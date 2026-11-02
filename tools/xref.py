"""Find RIP-relative references to target RVAs in .text.

Usage: python -I xref.py <exe> <rva_hex> [<rva_hex> ...]
Candidate disp32 locations are confirmed by disassembling the containing
instruction (tries instruction starts up to 10 bytes before the disp).
"""
import sys

import capstone
import lief
import numpy as np


def main():
    exe = sys.argv[1]
    targets = [int(t, 16) for t in sys.argv[2:]]
    binary = lief.parse(exe)
    text = binary.get_section(".text")
    data = bytes(text.content)
    tbase = text.virtual_address
    arr = np.frombuffer(data, dtype=np.uint8)
    n = len(arr) - 4
    disp = (arr[:n].astype(np.int64) | (arr[1:n + 1].astype(np.int64) << 8)
            | (arr[2:n + 2].astype(np.int64) << 16) | (arr[3:n + 3].astype(np.int64) << 24))
    disp = np.where(disp >= 2**31, disp - 2**32, disp)
    pos_rva = np.arange(n, dtype=np.int64) + tbase
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    for target in targets:
        print(f"== xrefs to {target:#x}")
        seen = set()
        for tail in range(0, 5):
            cand = np.nonzero(pos_rva + 4 + tail + disp == target)[0]
            for c in cand:
                for back in range(1, 11):
                    s = c - back
                    if s < 0:
                        continue
                    ins = next(md.disasm(data[s:s + 16], tbase + s), None)
                    if ins is None or ins.size != back + 4 + tail:
                        continue
                    if "rip" not in ins.op_str:
                        continue
                    if ins.address in seen:
                        break
                    seen.add(ins.address)
                    print(f"  {ins.address:08x}  {ins.mnemonic} {ins.op_str}")
                    break


if __name__ == "__main__":
    main()
