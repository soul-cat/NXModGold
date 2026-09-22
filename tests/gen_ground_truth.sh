#!/usr/bin/env bash
# 用系统汇编器生成 test_reloc.cpp 里的 ARM64 参考编码。
# 这些编码不是手算的——改动参考指令时重跑本脚本，把输出贴回 test_reloc.cpp。
set -euo pipefail
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/gt.s" <<'EOF'
.text
.globl _start
_start:
    b    .+0x1000
    bl   .-0x100
    adr  x0, .+0x100
    adr  x5, .-0x200
    ldr  x17, .+8
    br   x17
    b.eq .+0x40
    cbz  x0, .+0x40
    cbnz x1, .-0x40
    tbz  x0, #3, .+0x40
    tbnz x2, #40, .-0x40
    ldr  x0, .+0x40
    stp  x29, x30, [sp, #-0x20]!
    mov  x29, sp
    sub  sp, sp, #0x40
    ret
EOF

# 注意：Mach-O 汇编器不接受字面量偏移的 adrp（"ADR/ADRP relocations must be
# GOT relative"），所以 ADRP 的参考编码在测试里用 encodeAdrp() 自造，
# 并由独立实现的 decodeAdrp() 交叉验证。
cc -c -arch arm64 "$TMP/gt.s" -o "$TMP/gt.o"
objdump -d "$TMP/gt.o" | awk '/^\s+[0-9a-f]+:/ {printf "%s  %s %s %s\n", $2, $3, $4, $5}'
