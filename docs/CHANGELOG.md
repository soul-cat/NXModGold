# CHANGELOG

## 2026-09-22

### feat(hud): 项目奠基 — 共享内存协议 / 追踪器 / 偏移生成器

确定技术路线：Switch 版 Silksong 为 Unity IL2CPP 构建，PC 端 BepInEx mod
（ShowDamage HealthBar、Completionist Map）无法移植，必须走原生注入 + ARM64 内联 hook。

**新增**

| 文件 | 说明 |
|------|------|
| `shared/hud_protocol.h` | 插件↔overlay 共享内存协议。seqlock 单写多读，归一化屏幕坐标 |
| `plugin/source/tracker.hpp` | 敌人状态追踪器。定长 24 槽，无分配，LRU + TTL 淘汰 |
| `tools/00-setup.sh` | devkitPro + .NET + Il2CppDumper 环境准备 |
| `tools/targets.json` | 需要从 dump.cs 提取的类/字段/方法清单 |
| `tools/30-gen-offsets.py` | dump.cs → `plugin/source/offsets.h` |
| `tests/test_tracker.cpp` | 10 项宿主机单元测试 |

**验证**

```
make -C tests test   →  10/10 通过（ASan + UBSan）
./tools/30-gen-offsets.py --dump <fixture>  →  13 个符号正确提取
```

**关键设计决策**

1. **归一化坐标 [0,1]** —— 插件不感知 overlay 分辨率，overlay 不感知游戏分辨率。
   掌机/底座切换、第三方分辨率 mod 都不破坏对齐。
2. **seqlock 而非互斥锁** —— hook 跑在游戏主线程，绝不能因 overlay 阻塞。
3. **maxHp 推断回退** —— 部分敌人读不到 maxHP 字段时，用首次观测 `hp + damage` 估计。
4. **所有 RVA 隔离在生成的 `offsets.h`** —— 该文件不入库，游戏更新后重新生成即可。
5. **v1 只做屏幕固定血条**，不做世界坐标跟随。理由见 `docs/tasks/hud-v1.md`。

**未决**（等待研究结论）
- SaltyNX 插件入口与 main NSO 基址获取 API
- libtesla 常驻层 / 输入穿透的具体实现
- Il2CppDumper 对 NSO 的 RVA 语义（相对什么基准）

---

## 2026-09-22（续）

### feat(plugin): ARM64 指令重定位 + hook 安装

SaltyNX 不提供内联 hook（全仓库 grep "hook" 零命中），NX-FPS 与 lsfg-nx
全是 GOT 导入表替换 —— 对 `TakeDamage` 这种 main 内部直接 `bl` 调用的函数
完全无效（没有 GOT 槽，没有 `.dynsym` 条目）。因此自己实现。

**设计立场：宁可拒绝，也不生成半对的代码。**

| 指令 | 处理 |
|---|---|
| `B` / `BL`（±128 MiB） | 重写立即数 |
| `ADR`（±1 MiB）/ `ADRP`（±4 GiB） | 重写立即数 |
| `B.cond` / `CBZ` / `TBZ` / `LDR-literal` | 一律拒绝 |

测试两层：参考编码来自 macOS 系统汇编器真实输出（`tests/gen_ground_truth.sh`
可复现），验证独立实现的解码器；再用解码器做性质测试 —— 重定位后解析出的
目标地址必须与重定位前完全一致。

**编译器抓到一个真 bug**：`-(int64_t)1 << 27` 实际是 `(-1) << 27`，
一元负号优先级高于移位。三个范围常量全中。

**测试抓到一个测试自身的 bug**：ADR 性质测试只有 34/20000 通过。
查下来是期望不切实际 —— ADR 范围 ±1MiB，而 from/to 撒在 1GiB 范围里。
结论值得记：**序言里出现 ADR，基本等于放弃该 hook**。

### feat(plugin): 入口与 hook 安装

三个硬约束支配了设计：

1. 拿不到 RWX（内核只给 R/RW/RX，`svcCreateCodeMemory` 零售 NPDM 里没有）
   → trampoline 池编译期预留在插件自己的 `.text`（被映射为 RX），
   用 `SaltySD_Memcpy` 走 debug SVC 绕过页权限写入。
   **此路生态内无先例**，实机首次运行即验证点。
2. `SaltySD_Memcpy` 只在 `main()` 期间可用（Core 在 `LoadPlugins` 后立刻 `Deinit`）
   → 所有 patch 一次装完。
3. `main()` 跑在游戏执行任何自身代码之前，IL2CPP 未初始化
   → hook 体只按固定偏移读字段，不碰 IL2CPP 运行时。

入口三个讲究：必须 return 不能 exit、自带 fake heap、`_start` 在 ELF 偏移 0。

### feat(overlay): 常驻层渲染

配方来自 Status-Monitor-Overlay 的 Mini 模式（`FullMode=false` /
`TeslaFPS` / `deactivateOriginalFooter` / `alphabackground=0x0` /
`requestForeground(false)`，且**永不调用 `hide()`**）。

framebuffer 选 1280x256：双缓冲 1280 KB，对 nx-ovlloader 的 6 MB applet 堆
是安全余量（全屏 720 要 3.6 MB，太紧）。宽度必须是 32 的倍数。

布局全部做成纯函数（`hud_layout.hpp`），实机上只剩 `drawRect` 调用。
11 项测试覆盖淡出曲线单调性、配色阈值、填充宽度夹取、条不越界、
同列不重叠、Boss/普通分列、过期隐藏、排序、防御性输入。

### chore: docs/ 纳入版本库

用户明确指示。研究结论与决策记录对接手者有价值，
且仓库为私有。`CLAUDE.md` 仍留在本地。

### docs: 新增 HANDOFF.md

给下一个接手者的文档：当前状态（哪些验证过、哪些没有）、
唯一外部阻塞、六个会浪费时间的坑（每条带一手出处）、
必须保持的不变量、有风险待实机验证的地方、建议推进顺序。
