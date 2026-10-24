# SilksongHUD v1.0.8

- 发布：2026-10-24
- 目标：Hollow Knight: Silksong Switch **v1.0.3**（`1.0.30000`，Build ID `FC9EA4CCC955D579`）
- 环境：Atmosphère（实机验证于 AMS 1.11.2 / 系统 22.5.0）

## 这一版的重点：与皮肤 Mod 共存

以前皮肤 Mod（SilkModLoader）和 SilksongHUD 都写 `exefs/subsdk9` + `main.npdm`，
TOMOD 会报冲突。v1.0.8 把它们拆成两个 exefs 模块：

| 文件 | 内容 |
|---|---|
| `exefs/subsdk8` | SilkModLoader（原皮肤包的 `subsdk9` 改名，排在 HUD 前） |
| `exefs/subsdk9` | SilksongHUD |
| `exefs/main.npdm` | 两个包**逐字节相同**的 union npdm（59 个 SVC，fs 全开） |

- 皮肤 IPS 原样使用：实机确认 `SilkModLoader - main = 0x8A3A000`，与 IPS 写死的常量一致。
- TOMOD 的规则是「同路径 + 不同内容才冲突」，所以两个包可以**先装 HUD 或先装皮肤**。
- 已实机验证：存档 1 与 4，血条 / 地图 / 皮肤同时正常。

> v1.0.8 的 `subsdk9` 与 v1.0.7 功能相同（沿用同一份完整构建）。
> 本次新增的是 npdm 能力、皮肤共存包与工具链；源码里的启动模块表日志会在下一次
> 完整重编（需要 Completionist Map 数据）后带进二进制。

## 资产

| 文件 | SHA-256 |
|---|---|
| `SilksongHUD-v1.0.8.zip` | `7FD476ACD99579CC18E6895CE08527669B10A8D40A6E8C5F2B433A15EF84860F` |
| `SilksongSkin-coexist.zip` | `B3F1AE4DD30047EE38083682B080FC17588B4C240AAC484DE132C602B9C6ED7B` |

## 安装

### TOMOD / NX-Mod-Manager

1. 安装 `SilksongHUD-v1.0.8.zip`；
2. 安装 `SilksongSkin-coexist.zip`（顺序反过来也可以）；
3. **不要**再装商店里的原始皮肤包，也**不要**给皮肤 Mod 开自动更新。

### 手动

把两个 zip 分别解压到 SD 卡根目录（合并 `atmosphere/` 目录）即可。

## 皮肤包的来源与许可

`SilksongSkin-coexist.zip` 只是对原始皮肤包做容器重排（`subsdk9`→`subsdk8`、
替换 `main.npdm`），皮肤运行时、资源、IPS 均未修改：

- 皮肤作者：**终焉苍龙**（B 站）；Switch 移植：**nice2cu093**
  （原发布：<https://www.bilibili.com/video/BV1AXhE6BEnz>）
- SilkModLoader / SilkRuntime：<https://github.com/nice2cu1/SilkRuntime>
  （**GPL-2.0**；重新分发其二进制时请保留许可证与源码获取方式）
- 重打包与共存方案：cool / SilksongHUD

皮肤资源的权利归原作者与移植者。若原作者要求下架，请删除该资产；本项目只提供
布局转换工具 `tools/85-repack-skin-coexist.py`，任何人都可以用自己的原始皮肤包生成。

## 已知限制

- 只支持游戏 v1.0.3；其它版本 Build ID 不匹配时 HUD 不会安装。
- 皮肤共存布局依赖 `subsdk8` 在前、`subsdk9` 在后；换序会让皮肤 IPS 跳到错误地址。
- 皮肤运行时来自第三方；它的行为与兼容性由 SilkRuntime / 皮肤作者维护。
