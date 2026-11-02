"""Find direct CALL/JMP rel32 sites targeting given RVAs in .text.

Usage: python -I callers.py <exe> <rva_hex> [...]
"""
import sys

import lief
import numpy as np


def main():
    binary = lief.parse(sys.argv[1])
    text = binary.get_section(".text")
    arr = np.frombuffer(bytes(text.content), dtype=np.uint8)
    tbase = text.virtual_address
    n = len(arr) - 5
    rel = (arr[1:n + 1].astype(np.int64) | (arr[2:n + 2].astype(np.int64) << 8)
           | (arr[3:n + 3].astype(np.int64) << 16) | (arr[4:n + 4].astype(np.int64) << 24))
    rel = np.where(rel >= 2**31, rel - 2**32, rel)
    dest = np.arange(n, dtype=np.int64) + tbase + 5 + rel
    for t in sys.argv[2:]:
        target = int(t, 16)
        hits = np.nonzero((dest == target) & ((arr[:n] == 0xE8) | (arr[:n] == 0xE9)))[0]
        print(f"== {target:#x}: " + ", ".join(f"{h + tbase:#x}({'call' if arr[h] == 0xE8 else 'jmp'})" for h in hits))


if __name__ == "__main__":
    main()
