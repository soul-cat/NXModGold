#!/usr/bin/env python3
"""在解包后的 Silksong main.bin 中定位 il2cpp 的 CodeRegistration / MetadataRegistration。

为什么需要它：Il2CppDumper / Il2CppInspector(Redux) 的自动搜索在 Unity 6000.0.50f1
（Silksong Switch）上全部失败——metadata 的 image 数(78)与二进制 codeGenModules 数(80)
解耦，工具的硬校验永远不成立。本工具用两条独立链路定位：

  CodeRegistration: "mscorlib.dll" 字符串 <- Il2CppCodeGenModule.moduleName <-
                    codeGenModules 数组 <- 结构体尾部的 codeGenModules 字段(-0x80)
  MetadataRegistration: .text 中引用 CodeRegistration 的 ADRP+ADD 指令对，
                    同函数内相邻的另一个 ADRP+ADD 目标即是

用法:
    ./tools/find-il2cpp-registrations.py [work/main.bin]
输出: 两行十六进制地址（喂给 Il2CppDumper 手动模式）。
"""

from __future__ import annotations

import struct
import sys

NSO_FLAT = sys.argv[1] if len(sys.argv) > 1 else "work/main.bin"
TEXT_END = 0x64B7000  # .text 结束（.rodata 起）；对其它版本需按 nsotool info 调整
DATAB = 0x7A43000  # .data 起点


def u64(buf: memoryview, off: int) -> int:
    return struct.unpack_from("<Q", buf, off)[0]


def i64(buf: memoryview, off: int) -> int:
    return struct.unpack_from("<q", buf, off)[0]


def find_all(buf: bytes, needle: bytes, lo: int, hi: int) -> list[int]:
    out, i = [], lo
    while True:
        i = buf.find(needle, i, hi)
        if i < 0:
            return out
        out.append(i)
        i += 8


def adrp_target(w: int, pc: int) -> tuple[int, int] | None:
    """解码 ADRP Xd, page；返回 (Xd, page)。"""
    if (w >> 31) != 1 or (w >> 24) & 0x1F != 0x10:
        return None
    immlo = (w >> 29) & 3
    immhi = (w >> 5) & 0x7FFFF
    imm = (immhi << 2) | immlo
    if imm & (1 << 20):
        imm -= 1 << 21
    return w & 0x1F, ((pc & ~0xFFF) + (imm << 12)) & 0xFFFFFFFFFFFF


def add_imm(w: int) -> tuple[int, int, int] | None:
    """解码 ADD Xd, Xn, #imm12{,LSL#12}；返回 (Xd, Xn, imm)。"""
    if (w >> 24) != 0x91:
        return None
    sh = (w >> 22) & 3
    return w & 0x1F, (w >> 5) & 0x1F, ((w >> 10) & 0xFFF) << (12 if sh else 0)


def is_module_struct_ptr(buf: bytes, v: int) -> bool:
    """v 是否指向一个合法 Il2CppCodeGenModule（首字段指向 *.dll 字符串）。"""
    if not (DATAB < v < len(buf) - 8):
        return False
    sp = u64(buf, v)
    if not (TEXT_END <= sp < DATAB):
        return False
    end = buf.find(b"\0", sp, sp + 64)
    if end <= 0:
        return False
    name = buf[sp:end]
    # Unity 6 有一个名为 __Generated 的 codegen 附加模块（无 .dll 后缀）
    return name.endswith(b".dll") or name == b"__Generated"


def main() -> int:
    data = open(NSO_FLAT, "rb").read()
    size = len(data)

    # ---- 链路一: 字符串 → 模块结构 → 数组 → CodeRegistration 尾字段
    s = data.find(b"mscorlib.dll\x00", TEXT_END)
    if s < 0:
        print("找不到 mscorlib.dll 字符串", file=sys.stderr)
        return 1
    code_reg = None
    for m in find_all(data, struct.pack("<Q", s), DATAB, size):
        # m = Il2CppCodeGenModule.moduleName 字段（结构体首字段）
        for a in find_all(data, struct.pack("<Q", m), DATAB, size):
            # a = codeGenModules 数组元素；回溯数组起点
            start = a
            while start - 8 >= DATAB and is_module_struct_ptr(data, u64(data, start - 8)):
                start -= 8
            n = 1
            while is_module_struct_ptr(data, u64(data, a + n * 8)):
                n += 1
            n += (a - start) // 8  # 加上 mscorlib 之前的元素
            for c in find_all(data, struct.pack("<Q", start), DATAB, size):
                # c = &codeGenModules 字段；v31 布局 17 字段，codeGenModules 为最后一个(+0x80)
                base = c - 0x80
                if base < DATAB:
                    continue
                if i64(data, c - 8) != n:  # codeGenModulesCount 必须等于数组长度
                    continue
                counts = [i64(data, base + o) for o in (0x10, 0x28, 0x58)]
                if all(0 < c2 < 0x1000000 for c2 in counts):
                    code_reg = base
                    break
            if code_reg:
                break
        if code_reg:
            break
    if not code_reg:
        print("CodeRegistration 定位失败", file=sys.stderr)
        return 1
    print(f"CodeRegistration: {code_reg:X}", file=sys.stderr)

    # ---- 链路二: .text 中引用 CodeReg 的 ADRP+ADD，邻位的 ADRP+ADD 目标即 MetaReg
    for pc in range(0, TEXT_END - 16, 4):
        r = adrp_target(struct.unpack_from("<I", data, pc)[0], pc)
        if not r:
            continue
        rd, page = r
        for k in (1, 2, 3, 4):
            a = add_imm(struct.unpack_from("<I", data, pc + 4 * k)[0])
            if a and a[1] == rd:
                if page + a[2] == code_reg:
                    # 在 ±0x40 指令窗口内收集其它 ADRP+ADD 的 .data 目标
                    for pc2 in range(max(0, pc - 0x40), min(TEXT_END, pc + 0x44), 4):
                        if pc2 == pc:
                            continue
                        r2 = adrp_target(struct.unpack_from("<I", data, pc2)[0], pc2)
                        if not r2:
                            continue
                        rd2, page2 = r2
                        for k2 in (1, 2, 3, 4):
                            a2 = add_imm(struct.unpack_from("<I", data, pc2 + 4 * k2)[0])
                            if a2 and a2[1] == rd2:
                                t = page2 + a2[2]
                                # MetaReg 特征: fieldOffsetsCount == typeDefSizesCount > 0
                                if DATAB < t < size and i64(data, t + 0x50) == i64(data, t + 0x60) > 0:
                                    print(f"MetadataRegistration: {t:X}", file=sys.stderr)
                                    print(f"{code_reg:X}")
                                    print(f"{t:X}")
                                    return 0
                                break
                break
    print("MetadataRegistration 定位失败", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
