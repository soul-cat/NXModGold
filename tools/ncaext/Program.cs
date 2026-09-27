// 从（可能压缩/增量更新的）Silksong NCA 里抽出 RomFS 文件。
// 用法: ncaext <prod.keys> <title.keys> <base.nca> <upd.nca|-> <输出文件> [RomFS内路径]
//       ncaext <prod.keys> <title.keys> <base.nca> <upd.nca|-> <输出目录> --list | --match <子串>...
//         --list 打印 RomFS 全部文件（大小 \t 路径）；--match 只解出路径含任一子串的文件（不区分大小写）
// 依赖 LibHac：nstool 不支持固件 16+ 的 NCA 压缩分区，LibHac 是唯一能完整读取的库。
using System;
using System.IO;
using LibHac.Common;
using LibHac.Common.Keys;
using LibHac.Fs;
using LibHac.Fs.Fsa;
using LibHac.FsSystem;
using LibHac.Tools.Fs;
using LibHac.Tools.FsSystem;
using LibHac.Tools.FsSystem.NcaUtils;

class Program
{
    static int Main(string[] args)
    {
        if (args.Length < 5)
        {
            Console.Error.WriteLine("用法: ncaext <prod.keys> <title.keys> <base.nca> <upd.nca|-> <输出> [RomFS内路径]");
            return 2;
        }
        string prodKeys = args[0], titleKeys = args[1], baseNca = args[2], updNca = args[3], outFile = args[4];
        string innerPath = args.Length > 5 ? args[5] : "/Data/il2cpp_data/Metadata/global-metadata.dat";

        if (args.Length > 5 && (args[5] == "--list" || args[5] == "--match"))
        {
            ListOrMatch(LoadKeySet(prodKeys, titleKeys), baseNca, updNca, args[4], args[5] == "--list", args[6..]);
            return 0;
        }

        if (args.Length > 3 && args[^1] == "--extract-all")
        {
            ExtractAll(prodKeys, titleKeys, baseNca, updNca, args[4]);
            return 0;
        }

        KeySet keyset = KeySet.CreateDefaultKeySet();
        ExternalKeyReader.ReadKeyFile(keyset, prodKeys, null, null, null);
        // 注入 titlekey：LibHac 的 ReadTitleKeys 对我们这份 title.keys 解析不出（原因未查），
        // 干脆自己解析 + 用 TitleKeks 解密后直接塞进 ExternalKeySet。
        // title.keys 每行: <RightsId 32hex>: <加密titlekey 32hex>: <世代>
        foreach (var line in File.ReadAllLines(titleKeys))
        {
            var parts = line.Split(':', StringSplitOptions.TrimEntries);
            if (parts.Length < 3 || parts[0].Length != 32 || parts[1].Length != 32) continue;
            int gen = int.Parse(parts[2]);
            var rid = Convert.FromHexString(parts[0]);
            var enc = Convert.FromHexString(parts[1]);
            // ExternalKeySet 存加密 titlekey，LibHac 在 GetDecryptedTitleKey 里用 TitleKeks 自行解密
            keyset.ExternalKeySet.Add(new RightsId(rid), new LibHac.Spl.AccessKey(enc));
            Console.Error.WriteLine($"[debug] titlekey {parts[0]} gen {gen} 已注入(加密形态)");
        }

        // 存储不能用 using 包住：romfs 是惰性读取，读文件时才访问底层 NCA，
        // 提前 Dispose 会在 CopyTo 时抛 ObjectDisposedException。进程退出时自然释放。
        var baseN = new Nca(keyset, new LocalStorage(baseNca, FileAccess.Read));
        IFileSystem romfs = OpenRomFs(keyset, baseN, updNca);

        if (!romfs.FileExists(innerPath))
        {
            var tried = new[] { innerPath, "/Metadata/global-metadata.dat", "/global-metadata.dat",
                                "/Data/il2cpp_data/Metadata/global-metadata.dat" };
            foreach (var cand in tried)
            {
                var probe = new UniqueRef<IFile>();
                if (romfs.OpenFile(ref probe, cand.ToU8Span(), OpenMode.Read).IsSuccess())
                {
                    Console.Error.WriteLine($"[hit] 实际路径: {cand}");
                    innerPath = cand;
                    goto found;
                }
            }
            Console.Error.WriteLine("候选路径全部失败");
            return 1;
        }
        found:;
        var file = new UniqueRef<IFile>();
        romfs.OpenFile(ref file, innerPath.ToU8Span(), OpenMode.Read).ThrowIfFailure();
        using var stream = file.Get.AsStream();
        using var outF = File.Create(outFile);
        stream.CopyTo(outF);
        Console.WriteLine($"{innerPath} -> {outFile} ({outF.Length} bytes)");
        return 0;
    }

    /// <summary>
    /// 打开 RomFS；有更新档时叠加增量。
    /// 注意方向：在【基础版】上调用、把【更新档】作为参数（与 Ryujinx 的
    /// mainNca.OpenFileSystemWithPatch(patchNca) 一致）。反过来调用会把基础版当成
    /// 补丁盖在更新档上，得到的是基础版内容——曾因此拿到 v1.0.0 的 metadata。
    /// </summary>
    static IFileSystem OpenRomFs(KeySet keyset, Nca baseN, string updNca)
    {
        if (updNca == "-")
            return baseN.OpenFileSystem(NcaSectionType.Data, IntegrityCheckLevel.None);
        var updN = new Nca(keyset, new LocalStorage(updNca, FileAccess.Read));
        return baseN.OpenFileSystemWithPatch(updN, NcaSectionType.Data, IntegrityCheckLevel.None);
    }

    static KeySet LoadKeySet(string prodKeys, string titleKeys)
    {
        var keyset = KeySet.CreateDefaultKeySet();
        ExternalKeyReader.ReadKeyFile(keyset, prodKeys, null, null, null);
        foreach (var line in File.ReadAllLines(titleKeys))
        {
            var parts = line.Split(':', StringSplitOptions.TrimEntries);
            if (parts.Length < 3 || parts[0].Length != 32 || parts[1].Length != 32) continue;
            keyset.ExternalKeySet.Add(new RightsId(Convert.FromHexString(parts[0])),
                                      new LibHac.Spl.AccessKey(Convert.FromHexString(parts[1])));
        }
        return keyset;
    }

    /// 列出 RomFS 全部文件，或只解出路径含任一 needle 的文件（保持 RomFS 内的目录结构）
    static void ListOrMatch(KeySet keyset, string baseNca, string updNca, string destDir, bool isList, string[] needles)
    {
        var baseN = new Nca(keyset, new LocalStorage(baseNca, FileAccess.Read));
        IFileSystem romfs = OpenRomFs(keyset, baseN, updNca);
        foreach (var entry in romfs.EnumerateEntries("/", "*", SearchOptions.RecurseSubdirectories))
        {
            if (entry.Type != DirectoryEntryType.File) continue;
            if (isList)
            {
                Console.WriteLine($"{entry.Size}\t{entry.FullPath}");
                continue;
            }
            if (!Array.Exists(needles, n => entry.FullPath.Contains(n, StringComparison.OrdinalIgnoreCase))) continue;
            var file = new UniqueRef<IFile>();
            romfs.OpenFile(ref file, entry.FullPath.ToU8Span(), OpenMode.Read).ThrowIfFailure();
            string outPath = System.IO.Path.Combine(destDir, entry.FullPath.TrimStart('/'));
            Directory.CreateDirectory(System.IO.Path.GetDirectoryName(outPath)!);
            using var stream = file.Get.AsStream();
            using var outF = File.Create(outPath);
            stream.CopyTo(outF);
            Console.WriteLine($"{entry.FullPath} -> {outF.Length}");
        }
    }

    static void ExtractAll(string prodKeys, string titleKeys, string baseNca, string updNca, string destDir)
    {
        var keyset = LoadKeySet(prodKeys, titleKeys);
        using (var bs = new LocalStorage(baseNca, FileAccess.Read))
        {
            var baseN = new Nca(keyset, bs);
            IFileSystem romfs = OpenRomFs(keyset, baseN, updNca);
            FileSystemExtensions.Extract(romfs, destDir, null);
            Console.WriteLine($"extracted -> {destDir}");
        }
    }
}
