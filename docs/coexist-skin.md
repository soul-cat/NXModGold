# 与皮肤 Mod（SilkModLoader）共存

Silksong 的皮肤 Mod（例如「丝之歌 x 星见雅皮肤」）和 SilksongHUD 都通过
`atmosphere/contents/010013C00E930000/exefs/` 注入。一个 title 的 exefs 只能有一份
`subsdk9` 和一份 `main.npdm`，所以原始包直接安装会冲突。

本项目的共存方案是**两个 exefs 模块 + 一份共享的 union npdm**，不改任何一方的代码。

## 最终布局

| 路径 | 内容 | 来源 |
|---|---|---|
| `exefs/subsdk8` | SilkModLoader（皮肤运行时） | 原始皮肤包的 `exefs/subsdk9` 改名 |
| `exefs/subsdk9` | SilksongHUD | 本项目构建 |
| `exefs/main.npdm` | union npdm（59 个 SVC，fs 全开） | 游戏原版 npdm + `tools/40-patch-npdm.py` |
| `romfs/SilkModLoader/Mods/Skin/<皮肤>/...` | 皮肤资源 | 原始皮肤包 |
| `exefs_patches/SilkModLoader/<build-id>.ips` | 皮肤预启动 IPS | 原始皮肤包，原样使用 |

### 为什么皮肤必须是 `subsdk8`

rtld/loader 按固定顺序枚举 `subsdk0…subsdk9` 并依次分配地址。游戏自带
`rtld`、`subsdk0`、`sdk`，所以实测布局是：

```
idx0  rtld
idx1  main
idx2  (游戏自带模块)
idx3  SilkModLoader   <- 注入的第一个 subsdk，Subsdk1
idx4  sshud           <- 注入的第二个 subsdk，Subsdk2
idx12 Sdk             (地址最高的模块被 exlaunch 归到 Sdk)
```

皮肤包里的 IPS 把游戏调用点写死跳到 `main + 0x8A3A000 + hook_offset`。
按上面的顺序，`SilkModLoader - main = 0x8A3A000`，与 IPS 一致。
如果把 HUD 放在皮肤前面，皮肤的基址会后移，IPS 会跳到错误地址，游戏启动即崩。
另外 SilkRuntime 的 `DiscoverBases()` 在拿不到模块名时会回退到 `Subsdk1`，
这个顺序也正好保住它的回退路径。

## union npdm

两个模块共用进程的同一份 npdm。对比两边的能力集：

| | 我们原来的 | 皮肤自带的 | union |
|---|---|---|---|
| SVC 数量 | 54 | 57 | 59 |
| 我们独有 | `0x40 svcCreateSession`、`0x43 svcReplyAndReceive` | | 保留 |
| 皮肤独有 | | `0x4B`、`0x4C`、`0x73`、`0x77`、`0x78` | 追加 |
| fs 权限 | `0xffffffffffffffff` | `0x4000000000000000` | 全开 |
| 服务访问列表（SAC） | — | — | 两边逐字节相同 |

`tools/40-patch-npdm.py` 的 `REQUIRED_SVCS` 已包含这 9 个 SVC，生成的 npdm 同时满足两边。

## 重新打包皮肤包

```bash
# 1) 生成 union npdm（需要游戏原版 main.npdm，来自自己的转储）
python3 tools/40-patch-npdm.py patch \
    --in work/dump/ExeFS/main.npdm --out work/release/main.npdm

# 2) 只改容器布局：subsdk9 -> subsdk8，替换 main.npdm
./tools/85-repack-skin-coexist.py \
    --skin-zip <原始皮肤包.zip> \
    --npdm work/release/main.npdm \
    --out work/release/SilksongSkin-coexist.zip
```

脚本会校验原始包是否包含 `exefs/subsdk9`、`exefs/main.npdm`、唯一的皮肤目录和 IPS，
然后原样保留 `romfs/` 与 `exefs_patches/`。

## TOMOD 的冲突规则

TOMOD 的 FAQ 明确：**同一路径 + 内容不同 = 冲突；内容相同 = 共享（引用计数）**。
所以：

- 两个包的 `main.npdm` 必须**逐字节相同**（都放 union npdm）；
- `subsdk8` / `subsdk9` 各自独立；
- `romfs/`、`exefs_patches/` 不重叠。

满足以上条件后，**先装 HUD 再装皮肤，或先装皮肤再装 HUD 都可以**。

注意事项：

1. 不要再用 TOMOD 商店里的原始皮肤包（它带自己的 `subsdk9` 和 npdm）。
2. 不要对皮肤 Mod 开启自动更新；商店更新会覆盖回原始布局。
3. 如果 union npdm 以后再加 SVC，两个包要一起更新，保持 npdm 字节一致。

## 实机验证记录

2026-10-24，v1.0.3（Build ID `FC9EA4CCC955D579`）：

```
main                0x930a06000
SilkModLoader       0x939440000   delta 0x8A3A000
sshud               0x93a089000
build id ... -> match
map hooks installed: 571 collectibles
options / delivery / crest / boss rush hooks installed
```

存档 1 与 4 均正常，皮肤与 HUD 同时生效。

## 版权与许可

- 皮肤资源：终焉苍龙（B 站），Switch 移植：nice2cu093。资源权利归原作者/移植者；
  公开分发前请确认授权。
- SilkModLoader 来自 [SilkRuntime](https://github.com/nice2cu1/SilkRuntime)，GPL-2.0；
  重新分发其二进制时需附带许可证与源码获取方式（上面的仓库链接）。
- 本项目的 HUD 代码与 union npdm 由本项目作者维护。
