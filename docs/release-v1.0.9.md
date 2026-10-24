# SilksongHUD v1.0.9 — 皮肤主题

- 发布：2026-10-24
- 目标：Hollow Knight: Silksong Switch **v1.0.3**（`1.0.30000`，Build ID `FC9EA4CCC955D579`）
- 环境：Atmosphère

## 这一版的变化

- **自动检测皮肤**：启动时扫 rtld 模块表，检测到 `SilkModLoader` 就把血条切到皮肤主题；
  没有皮肤时保持原来的红白配色。日志会写 `bar theme: skin` / `default`。
- **皮肤配色**：青蓝填充 `#55C8F0`、暗青底 `#0A1418`、淡青 BOSS 外框 `#D8F4FF`；
  伤害数字 `#FF4A6A`；菜单开启项青蓝。
- **低血量平滑过渡**：血量从 65% 到 35% 用 smoothstep 混合到暗红 `#C03050`，
  不再在 49% 处硬切；数字颜色同步过渡。
- **1px 高光**：填充顶部加一条细亮边。
- **删除重复名字**：BOSS 条上方不再画猎人日志名字；游戏自己的左下角大标题保留、未改动。
- **地图收集物恢复**：完整构建，571 条标记（18 类）。

> v1.0.9 的 `main.npdm` 与 v1.0.8 相同（union npdm），因此皮肤共存包不用重发，
> 继续用 v1.0.8 的 `SilksongSkin-coexist.zip` 即可；两个包的安装顺序仍然不限。

## 资产

| 文件 | SHA-256 |
|---|---|
| `SilksongHUD-v1.0.9.zip` | `60333CD1EA021EE1A4DE4661BEC227EF578F6555591EF1063F3A5E0D6A3C79DB` |
| `SilksongSkin-coexist.zip`（沿用 v1.0.8） | `B3F1AE4DD30047EE38083682B080FC17588B4C240AAC484DE132C602B9C6ED7B` |

## 安装

### TOMOD / NX-Mod-Manager

1. 安装 `SilksongHUD-v1.0.9.zip`；
2. 安装 v1.0.8 的 `SilksongSkin-coexist.zip`（顺序反过来也可以）；
3. 不要再用商店里的原始皮肤包，也不要给皮肤 Mod 开自动更新。

### 手动

两个 zip 分别解压到 SD 卡根目录（合并 `atmosphere/` 目录）。

## 调色板

颜色和过渡区间在 `exl/program/hud_theme_model.hpp`：

- `LOW_HEALTH_FADE_START = 0.65`、`LOW_HEALTH_FADE_END = 0.35`；
- 默认主题不受影响（正常/低血量填充同色）；
- 皮肤主题只在检测到 `SilkModLoader` 时启用。

## 皮肤包来源与许可

`SilksongSkin-coexist.zip` 只是对原始皮肤包做容器重排（`subsdk9`→`subsdk8`、
替换 `main.npdm`），皮肤运行时、资源、IPS 均未修改：

- 皮肤作者：**终焉苍龙**（B 站）；Switch 移植：**nice2cu093**
  （原发布：<https://www.bilibili.com/video/BV1AXhE6BEnz>）
- SilkModLoader / SilkRuntime：<https://github.com/nice2cu1/SilkRuntime>（**GPL-2.0**）

皮肤资源的权利归原作者与移植者；若原作者要求下架，请删除该资产。
本项目只提供布局转换工具 `tools/85-repack-skin-coexist.py`。

## 已知限制

- 只支持游戏 v1.0.3；其它版本 Build ID 不匹配时 HUD 不会安装。
- 皮肤主题依赖 `SilkModLoader` 模块存在；换皮肤包/卸载皮肤后需要重启游戏。
- 游戏自己的左下角 BOSS 大标题没有染色，仍使用游戏原色。
