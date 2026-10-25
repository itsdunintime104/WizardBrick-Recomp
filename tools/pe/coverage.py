"""Measure how much of the real code region of the PE is covered by Ghidra function bodies.

Usage: coverage.py <ghidra_functions.json> <ghidra_text_data.json>
The region is 0x401000..0xac3787 (real code; the rest of .text is 0xCC padding).
Function bodies are unioned (a body is taken as the span min..max, clipped to the region),
so overlapping bodies are never counted twice.
"""
import json
import sys

REGION_START = 0x401000
REGION_END = 0xAC3787  # exclusive


def union(spans):
    out = []
    for lo, hi in sorted(spans):
        if out and lo <= out[-1][1]:
            if hi > out[-1][1]:
                out[-1][1] = hi
        else:
            out.append([lo, hi])
    return out


def clip(lo, hi):
    return max(lo, REGION_START), min(hi, REGION_END)


def main(argv):
    if len(argv) != 3:
        sys.exit("usage: coverage.py <functions.json> <text_data.json>")
    with open(argv[1]) as f:
        funcs = json.load(f)
    with open(argv[2]) as f:
        data = json.load(f)

    # Use the exact body ranges when the export has them. An older export has only
    # min/max/size; a body with size < max-min+1 is then counted as `size` bytes from min.
    spans = []
    fragmented = 0
    for fn in funcs:
        if "ranges" in fn:
            for lo, hi in fn["ranges"]:
                lo, hi = clip(lo, hi + 1)
                if lo < hi:
                    spans.append((lo, hi))
            continue
        lo, hi = fn["min"], fn["max"] + 1
        if fn["size"] < hi - lo:
            fragmented += 1
            hi = lo + fn["size"]
        lo, hi = clip(lo, hi)
        if lo < hi:
            spans.append((lo, hi))
    covered = union(spans)
    covered_bytes = sum(hi - lo for lo, hi in covered)
    region = REGION_END - REGION_START

    dspans = []
    for d in data:
        lo, hi = clip(d["addr"], d["addr"] + d["length"])
        if lo < hi:
            dspans.append((lo, hi))
    dunion = union(dspans)
    data_bytes = sum(hi - lo for lo, hi in dunion)

    gaps = []
    pos = REGION_START
    for lo, hi in covered:
        if lo > pos:
            gaps.append((pos, lo - pos))
        pos = max(pos, hi)
    if pos < REGION_END:
        gaps.append((pos, REGION_END - pos))
    uncovered = sum(n for _, n in gaps)

    # data bytes that lie in gaps (not already inside a function body)
    data_in_gaps = 0
    for glo, n in gaps:
        ghi = glo + n
        for lo, hi in dunion:
            a, b = max(lo, glo), min(hi, ghi)
            if a < b:
                data_in_gaps += b - a

    print(f"region: 0x{REGION_START:x}..0x{REGION_END:x} = {region} bytes")
    print(f"function count: {len(funcs)} ({fragmented} old-format bodies with holes)")
    print(f"covered bytes: {covered_bytes}")
    print(f"coverage percent: {100.0 * covered_bytes / region:.2f}")
    print(f"data bytes in region: {data_bytes} (of which in gaps: {data_in_gaps})")
    print(f"remaining uncovered bytes: {uncovered} in {len(gaps)} gaps")
    print("10 largest remaining gaps:")
    for lo, n in sorted(gaps, key=lambda g: -g[1])[:10]:
        print(f"0x{lo:x} {n}")


if __name__ == "__main__":
    main(sys.argv)
