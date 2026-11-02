"""Disassemble a range of the game executable.

Usage: python -I disasm.py <exe> <rva_hex> [before_bytes] [after_bytes]
Starts `before_bytes` ahead of the RVA (may desync on the first lines).
"""
import sys

import capstone
import lief


def main():
    exe = sys.argv[1]
    rva = int(sys.argv[2], 16)
    before = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x40
    after = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0x80
    binary = lief.parse(exe)
    base = binary.optional_header.imagebase
    start = rva - before
    code = bytes(binary.get_content_from_virtual_address(base + start, before + after))
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    for ins in md.disasm(code, start):
        mark = ">>" if ins.address == rva else "  "
        print(f"{mark} {ins.address:08x}  {ins.bytes.hex():<24} {ins.mnemonic} {ins.op_str}")


if __name__ == "__main__":
    main()
