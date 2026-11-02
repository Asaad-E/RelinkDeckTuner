"""Scan the game executable for IDA-style byte patterns (?? = wildcard).

Usage: python -I patscan.py <exe> <patterns.txt>
patterns.txt: one "name|pattern" per line. Reports every match as RVA.
"""
import re
import sys

import lief


def compile_pattern(pat):
    parts = []
    for tok in pat.split():
        parts.append(b"." if tok == "??" else re.escape(bytes([int(tok, 16)])))
    return re.compile(b"".join(parts), re.DOTALL)


def main():
    exe, patfile = sys.argv[1], sys.argv[2]
    binary = lief.parse(exe)
    text = binary.get_section(".text")
    data = bytes(text.content)
    base_rva = text.virtual_address
    for line in open(patfile, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, pat = line.split("|", 1)
        hits = [base_rva + m.start() for m in compile_pattern(pat).finditer(data)]
        shown = ", ".join(hex(h) for h in hits[:5])
        print(f"{name}: {len(hits)} match(es) {shown}")


if __name__ == "__main__":
    main()
