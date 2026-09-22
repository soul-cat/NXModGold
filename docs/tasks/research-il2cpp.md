# 研究结论：IL2CPP dump 流水线

来源：`il2cpp-research` agent，核对到 Il2CppDumper 源码与 Silksong PC 反编译。

## 最关键的一条：RVA 锚定在模块基址

```
runtime_address = module_base + RVA        ← 全部算式，不减任何东西
```

**依据（两条独立证据）**

1. `Il2Cpp.GetRVA` 默认恒等（`Il2Cpp.cs:343`）：
   - PE 覆写为 `pointer - ImageBase`
   - Macho64 覆写为 `pointer - vmaddr`
   - Elf64 覆写为 `IsDumped ? pointer - ImageBase : pointer`
   - **NSO.cs 根本没覆写** → `RVA == VA == 二进制里的原始指针`

2. `NSO.MapVATR` 是纯段运算，全程没有 image base：
   ```csharp
   var segment = segments.First(x => addr >= x.MemoryOffset && addr <= x.MemoryOffset + x.DecompressedSize);
   return addr - segment.MemoryOffset + segment.FileOffset;
   ```
   且 `RelocationProcessing()` 把 `R_AARCH64_RELATIVE` 的 addend **原样写入**，
   不是 `base + addend`，所以 .data 里的函数指针也保持模块相对。

**独立交叉验证**：现有 Silksong 金手指（tomvita/MyNXCheats，BID `DC3E4B892043EF23`）
在 `main+0x0230FE9C` 写入 `14F4A4F9`，解码为 `B +0x3D293E4`，
落点 `0x0230FE9C + 0x3D293E4 = 0x6039280` —— 正好是它自己的 codecave。
说明 Atmosphère region M=0 的偏移、`.text`、以及远在 `.bss` 的 codecave
是同一个平坦的模块相对地址空间，与 RVA 同币种。

> **已据此修掉 `nsotool.py` 里的错误公式**（原先打印 `模块基址 + (VA - text.memOff)`）。
> 实践上 `.text memOff` 恒为 0 所以是 no-op，但原则上错，且与下方 `flat_image()` 自相矛盾。

## 修掉的另外两个问题

**`nstool --raw --part0` 这个命令不存在。** nstool 的 `-x/--extract` 只支持文件系统容器
（PartitionFs / Sha256PartitionFs / RomFs / NCA / NSP / XCI），NSO 可读但不可 extract。
macOS arm64 上目前没有已验证的 zstd NSO 解压方案。错误文案已改成如实说明。

**`flat_image()` 漏了 `.bss`。** RVA 可以指到 `.data` 之后（比如上面那个 codecave），
原先 `image[rva]` 会越界。已补 `image_size` 属性把 `bss_size` 算进去并零填充。

## 目标类结构（PC 版反编译，Switch 版类结构相同）

```csharp
public class HealthManager : MonoBehaviour, IHitResponder, ITagDamageTakerOwner, IInitialisable
{
    public  int hp;        // 当前血量
    private int initHp;    // 最大血量 —— 没有 maxHP/MaxHP
    public  bool isDead;
    private bool invincible;
    private HealthManager sendDamageTo;

    public  IHitResponder.HitResponse Hit(HitInstance);   // :371 公开入口
    private HitInstance ApplyDamageScaling(HitInstance);  // :547
    private void TakeDamage(HitInstance);                 // :568 单一重载
    public  void Die(...);                                // 3 个重载
}

[Serializable] public struct HitInstance {   // struct，不是 class
    public int  DamageDealt;     // :248
    public bool IsHeroDamage;    // :335
    public float Multiplier;     // :302
}
```

`initHp` 是最大血量的证据：`initHp = hp`（:234，init 时捕获一次）、
`hp = initHp`（:1824 / :1848，复活时还原）。

**注意**：PC 版血条 mod `XiaoHaiiOvO/Silksong_HealthBar` **没有读 initHp**，
它用 `Mathf.Max(打前, 打后)` 推断最大值。所以 initHp 只确认了「声明存在」
和「可按名访问」（KyuubiRan 的 mod 用了 `AccessTools.FieldRefAccess`），
没见过有人在运行时真读它。风险低，但记一笔。

## hook 点选择

| | `TakeDamage` | `Hit` |
|---|---|---|
| 可见性 | private | public |
| 伤害值 | ApplyDamageScaling **之后**（最终值） | 之前 |
| 被裁剪风险 | 低 | **零**（接口实现，有 vtable slot） |
| 重载 | 单一 | 单一 |

主 hook 用 `TakeDamage`（拿到最终伤害）。若 dump 里它报 `RVA: -1`，
改用 `Hit`，并可通过 vtable slot 兜底解析。

⚠️ `HitInstance` 是大号 by-value struct，ARM64 ABI 下经**隐藏指针**传递
（`this` 在 x0，struct 指针在 x1），不是寄存器传参。写 hook 前先在反汇编确认。

## RVA -1 风险评估

`Il2CppDecompiler.cs` 在 `isAbstract || methodPointer <= 0` 时打印 `RVA: -1`。
零指针来自 `Il2CppCodeGenModule.methodPointers`，Unity 对以下情况写 0：
抽象/接口声明、泛型方法定义、完全泛型共享的方法体、linker 移除的方法
（但完全 strip 会把方法从 metadata 里删掉，表现为 dump.cs 里**根本没有**，而非 -1）。

`HealthManager.TakeDamage` 一条都不沾：非抽象、非泛型、类非泛型非抽象，
且被 `Hit()` 无条件调用。**风险接近零。**

搜索结果也没有 Unity 6 / v31 大面积 -1 的报告。
（Issue #894 "Not supported for Unity 6000" 是 Android 上 metadata 被并进 libunity
的打包问题，Switch 不适用 —— `main` 就是全部二进制，metadata 是 romfs 里的明文文件。）

**缓解措施已内建**：`20-dump-il2cpp.sh` 统计 `RVA: -1` 占比，超过 40% 就警告
metadata 解析可能出错，并逐条确认 4 个关键符号是否存在。

## 工具链

- Silksong = Unity **6000.0.50f1** → metadata **v31**
- 上游 `Perfare/Il2CppDumper` v6.7.46 `Metadata.cs:55` 是
  `if (version < 16 || version > 31) throw` → v31 在范围内，**用上游即可**
- v35–v39 的分支（c01ns 是真正做 v39 的，Doppelglower 从它派生但只改输出质量）
  是给 Unity 6000.3+ 的，我们用不上
- **Il2CppDumper 内部解压 LZ4 NSO**：`Program.cs` 对 magic `0x304F534E` 调
  `nso.UnCompress()`，不需要 hactool/nx2elf/nsnsotool

### macOS arm64 运行要点

```bash
brew install --cask dotnet-sdk      # 用 cask 不用 formula
DOTNET_ROLL_FORWARD=LatestMajor dotnet Il2CppDumper.dll <main> <metadata> <outdir>
```

四个会卡住人的细节（已全部写进 `20-dump-il2cpp.sh`）：

1. `DOTNET_ROLL_FORWARD=LatestMajor` **必须设** —— 预编译包 target net6/net7
2. **输出目录必须先存在**，否则被静默忽略、产物落到 dll 旁边
3. `config.json` 必须和 dll 同目录（从 `AppDomain.CurrentDomain.BaseDirectory` 读）
4. `config.json` 的 `RequireAnyKey` 要设 `false`，否则结束时卡在 `Console.ReadKey`

参数顺序无所谓：metadata 靠 magic `0xFAB11BAF` 识别，目录识别为输出目录。

## 未能确认

- Unity 6 时代 Switch romfs 的 metadata 路径（`Data/Managed/Metadata/` vs
  `Data/il2cpp_data/Metadata/`）—— `10-extract.sh` 用 `find` 规避
- Silksong 的 NSO 是否使用 zstd/zbic 压缩 —— 若使用，Il2CppDumper、Redux、
  我们的 nsotool 三者全部卡住
- `LocalisedString` 所属程序集（推测 TeamCherry.Localization）
- `initHp` 未见有人在运行时真读过
