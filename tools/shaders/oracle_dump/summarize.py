"""Summarise a shader oracle dump.

Usage: python -I summarize.py <dump dir> <out.json> [cache dir] [GAME.DAT]

<dump dir> holds the proxy's <index>.{json,hlsl,bin,err} files. The optional
cache dir is the game's CachedShaders folder copied out by run.sh. With
GAME.DAT, the cache names are compared with the CachedShaders file names that
the developer build listed in the archive's text file table.
"""
import collections
import hashlib
import json
import os
import re
import sys


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def main():
    dump, out = sys.argv[1], sys.argv[2]
    cache = sys.argv[3] if len(sys.argv) > 3 else None
    game_dat = sys.argv[4] if len(sys.argv) > 4 else None

    calls = []
    for name in sorted(os.listdir(dump)):
        if name.endswith(".json"):
            with open(os.path.join(dump, name)) as f:
                calls.append(json.load(f))

    sources, keys, codes = set(), set(), set()
    count = collections.Counter
    profiles, flags, entries, hrs = count(), count(), count(), count()
    warnings = 0
    for c in calls:
        stem = os.path.join(dump, "%06d" % c["index"])
        src = sha(stem + ".hlsl") if os.path.exists(stem + ".hlsl") else None
        sources.add(src)
        keys.add((src, c["entry"], c["profile"], c["flags"]))
        if os.path.exists(stem + ".bin"):
            codes.add(sha(stem + ".bin"))
        if os.path.exists(stem + ".err"):
            warnings += 1
        profiles[c["profile"]] += 1
        flags["0x%x" % c["flags"]] += 1
        entries[c["entry"]] += 1
        hrs["0x%08x" % (c["hr"] & 0xFFFFFFFF)] += 1

    summary = {
        "total_compiles": len(calls),
        "unique_sources": len(sources),
        "unique_source_entry_profile_flags": len(keys),
        "unique_bytecode": len(codes),
        "calls_with_messages": warnings,
        "with_defines": sum(1 for c in calls if c["defines"]),
        "with_include": sum(1 for c in calls if c["include"]),
        "hr": dict(hrs),
        "profiles": dict(profiles),
        "flags": dict(flags),
        "entry_points": dict(entries),
        "compile_ms_total": sum(c["ms"] for c in calls),
    }
    if cache:
        ours = {n.upper() for n in os.listdir(cache) if n.upper() != "VERSION.DAT"}
        summary["cache_files"] = len(ours)
        if game_dat:
            with open(game_dat, "rb") as f:
                text = f.read()
            dev = {m.decode().upper() for m in
                   re.findall(rb"CachedShaders\\([0-9A-Fa-f]{9}\.DAT)", text)}
            summary["dev_cache_names"] = len(dev)
            summary["cache_names_in_dev_list"] = len(ours & dev)
            summary["cache_names_not_in_dev_list"] = sorted(ours - dev)
    with open(out, "w") as f:
        json.dump(summary, f, indent=1)
        f.write("\n")
    print(json.dumps({k: v for k, v in summary.items()
                      if k != "cache_names_not_in_dev_list"}, indent=1))


if __name__ == "__main__":
    main()
