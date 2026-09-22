# 研究结论：Tesla 常驻 overlay

来源：`overlay-research` agent，全部核对到一手源码。

## 结论摘要

**上游 libtesla 做不到常驻 + 输入穿透，必须用 Ultrahand/Status-Monitor 的改造版。**

上游缺三样：framebuffer 尺寸写死 448x720、没有 `setLayerPos()`、
`OverlayFrame::draw` 里无条件 `fillScreen(黑色 alpha=0xD)` 并画 "Back / OK" 页脚。

## 常驻模式配方

在 Gui 构造函数里：

```cpp
FullMode = false;                    // 去掉全屏边框
TeslaFPS = <刷新率>;                 // 接管帧节奏（禁用内建 60Hz 限速）
deactivateOriginalFooter = true;     // 去掉 "Back / OK"
alphabackground = 0x0;               // fillScreen 变成透明清屏
tsl::hlp::requestForeground(false);  // 把输入还给游戏
```

析构时全部还原。**永远不要调用 `hide()`**。

## 输入机制（关键）

不是 applet 焦点，是 `hid:sys` cmd 503：

```cpp
requestForeground(false)
  → hidsysEnableAppletToGetInput(true, 游戏aruid)      // 恢复游戏输入权
  → hidsysEnableAppletToGetInput(true, 0)              // overlay 自身始终保留
```

**输入是复制而非移交** —— overlay 进程 `__nx_applet_type = AppletType_None`，
直接读 HID 共享内存。所以游戏和 overlay 会同时收到同一次按键。

> 对我们的影响：HUD 的开关热键也会传进游戏。选组合键时必须避开游戏用到的键。

## 坐标空间（对我们最重要）

```
ScreenWidth/Height    = 1920 x 1080   （vi 默认显示，掌机与底座相同）
LayerMaxWidth/Height  = 1280 x 720    （framebuffer 参考空间）
LayerWidth  = 1920 * (FBW/1280)       → 恒定 1.5x 上采样
```

**framebuffer 像素 (x,y) ≡ 720p 屏幕空间像素 (x,y)。**

> 这直接验证了我们协议里归一化坐标的设计：
> `screenX * 1280` 和 `screenY * 720` 可以直接喂给 `drawRect`。
> 见 `overlay/source/hud_reader.hpp` 的 `normToScreenX/Y`。

约束：
- `framebufferWidth` 必须是 **32 的倍数**（libnx 把 stride 对齐到 64 字节，
  而 libtesla 的 swizzle 假设 `stride == FramebufferWidth`）
- 尺寸**只在 `Renderer::init()` 读一次**，必须在 `tsl::loop<T>()` 之前设好，
  运行时改不了；位置 `setLayerPos()` 可以随时改
- 底座输出非像素精确（720p 上采样到 1080p）。Ultrahand 有
  `windowedLayerPixelPerfect` 修正，SMO 的 vendored 版本没有

## 内存预算

nx-ovlloader 的 applet 堆 `g_appletHeapSize = 0x600000`（6 MB）。

| framebuffer | 单缓冲 | 双缓冲 | 评价 |
|---|---|---|---|
| 1280x720 全屏 | 1.8 MB | 3.6 MB | 对 6MB 堆偏紧 |
| 1280x256 条带 | 640 KB | 1.3 MB | **v1 选这个** |
| 1280x28 (Micro 同款) | 70 KB | 140 KB | 太矮，放不下多条血条 |

RGBA4444，16 bits/px，双缓冲，block-linear swizzle。

## 绘图

`tsl::elm::CustomDrawer` 的回调每帧跑一次，SMO 版本**不开 scissoring**，
可以在整个 framebuffer 上任意画。仍需 `OverlayFrame` 包一层，
因为它的 `fillScreen` 是每帧清屏的来源。

所有颜色必须用 `a(color)` 包一下（应用全局淡入淡出透明度）。

可用图元：`drawRect` / `drawEmptyRect` / `drawLine` / `drawDashedLine` /
`drawBitmap`(RGBA8888 源) / `drawString` / `setPixel` / `setPixelBlendSrc|Dst`。

## 刷新率

`TeslaFPS` 只是**关掉**内建限速，真正的节流在 Gui 的 `handleInput` 末尾
用 1ms 粒度 busy-sleep 实现。`endFrame()` 仍无条件 `waitForVSync()`，
所以 **60Hz 是硬上限**。

SMO 各模式默认 `refresh_rate = 1`，文档反复强调
"Higher value means higher CPU Core #3 usage"。nx-ovlloader 把 overlay 钉在 **core 3**。

> 对我们的影响：血条要跟手，1Hz 不够。打算用 20–30Hz 并做成可配置，
> 实机测 core3 占用后再定。

## 修正：dmntcht 不在 libnx 里

**`switch/services/dmntcht.h` 在 libnx 中不存在**（已核对 404）。
它来自 Atmosphère 的 libstratosphere，需要 vendor 进来：
`Atmosphere/libraries/libstratosphere/source/dmnt/dmntcht.{h,c}`。

`dmnt:cht` 另有一条硬限制：**最多 2 个并发会话**
（`dmnt_main.cpp: constexpr size_t CheatMaxSessions = 2;`）。
EdiZon / EdiZon-Overlay / 我们的 overlay 抢同一个服务，第三个会失败。

> **对我们的影响：无。**
> 我们走的是插件直接往 SharedMemory 写、overlay 只读 SharedMemory 的路线，
> 全程不碰 `dmnt:cht`，因此不占会话、不与 EdiZon 冲突。
> 这条只在退回"纯读内存"方案时才相关。

## 上游状态提醒

**Status-Monitor-Overlay 已停止维护**，README 指向
`masagrator/Status-Monitor-Deux`，后者基于 `ppkantorski/libultrahand`。
机制不变，但要拿活跃代码库应该看 Deux / libultrahand。

## 未能从一手源确认

- `viGetZOrderCountMax()` 在零售机上的返回值
- 掌机模式下 vi 默认显示确为 1920x1080（强烈暗示但无明文）
- Mini 模式 core3 的绝对 CPU 占用数字
- 除 2 会话上限外，EdiZon 与 overlay 的具体冲突行为
