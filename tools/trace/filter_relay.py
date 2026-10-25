"""Reduce a Wine relay log of the original exe to the calls the exe makes.

Usage:
    python -I tools/trace/filter_relay.py RELAY.log OUT.jsonl [--summary OUT.md]

The log comes from a run with
    WINEDEBUG=+relay,+seh,+loaddll,+threadname,+timestamp,+d3d9
and HKCU\\Software\\Wine\\Debug RelayFromInclude=LEGOHarryPotter.exe (with no
RelayFromExclude value: Wine ignores the include list when an exclude list
exists). See notes/golden_trace.md for the full recipe.

One JSONL record per event, in log order:
    {seq, ts, tid, dll, func, args[], ret, caller_va}
- Relay calls whose return address is in the exe image (0x400000..0x13B5000).
  `ret` is the return value from the matching "Ret" line (null if the call
  never returned). Optional `file` names the path of the handle in args[0]
  for file calls (ReadFile, SetFilePointer, ...).
- dll "d3d9.com": Direct3D 9 COM methods (vtable calls are not relayed; they
  come from the +d3d9 channel). Only the methods in D3D9_KEEP are kept.
- dll "seh": exceptions (32-bit side only), func = exception code.
- dll "thread": thread names from +threadname, args = [tid, name].
- dll "loaddll": DLL loads.
"""

import argparse
import json
import re
import sys
from collections.abc import Callable, Iterable
from typing import IO, Any

Record = dict[str, Any]

EXE_LO, EXE_HI = 0x400000, 0x13B5000
D3D9_KEEP = {"d3d9_CreateDevice", "d3d9_device_Present", "d3d9_device_PresentEx",
             "d3d9_swapchain_Present", "d3d9_device_Reset", "d3d9_device_ResetEx",
             "d3d9_device_TestCooperativeLevel", "d3d9_device_BeginScene"}
FILE_FUNCS = {"ReadFile", "WriteFile", "SetFilePointer", "SetFilePointerEx",
              "GetFileSize", "GetFileSizeEx", "CloseHandle", "ReadFileEx"}
# Pseudo-DLLs that are not calls made by the exe.
NOT_CALLS = {"seh", "thread", "loaddll", "d3d9.com"}

HEAD = r"^(?P<ts>\d+\.\d+):(?P<tid>[0-9a-f]{4}):"
RE_CALL = re.compile(HEAD + r"Call (?P<dll>[\w-]+)\.(?P<func>[\w@?$]+)\((?P<args>.*)\) ret=(?P<ra>[0-9a-f]+)$")
RE_RET = re.compile(HEAD + r"Ret  (?P<dll>[\w-]+)\.(?P<func>[\w@?$]+)\(\) retval=(?P<rv>[0-9a-f]+) ret=(?P<ra>[0-9a-f]+)$")
RE_D3D9 = re.compile(r"(?P<ts>\d+\.\d+):(?P<tid>[0-9a-f]{4}):trace:d3d9:(?P<func>\w+) (?P<rest>.*)$")
RE_SEH = re.compile(HEAD + r"trace:seh:dispatch_exception code=(?P<code>[0-9a-f]+)(?: \((?P<name>\w+)\))? flags=\w+ addr=(?P<addr>[0-9A-F]{8})$")
RE_TNAME = re.compile(HEAD + r"\w+:threadname:\w+ Thread ID (?P<id>[0-9a-f]+) renamed to \"(?P<name>.*)\"$")
RE_LOAD = re.compile(HEAD + r"trace:loaddll:\w+ Loaded (?P<what>.*)$")


def split_args(s: str) -> list[str]:
    """Split a relay argument list on commas outside quoted strings."""
    out: list[str] = []
    cur: list[str] = []
    quote = ""
    i = 0
    while i < len(s):
        c = s[i]
        if quote:
            cur.append(c)
            if c == "\\" and i + 1 < len(s):
                cur.append(s[i + 1])
                i += 1
            elif c == quote:
                quote = ""
        elif c in "\"'":
            quote = c
            cur.append(c)
        elif c == ",":
            out.append("".join(cur))
            cur = []
        else:
            cur.append(c)
        i += 1
    if cur or out:
        out.append("".join(cur))
    return out


def quoted(arg: str) -> str | None:
    """Text of the first quoted string in a relay argument, or None."""
    m = re.search(r'L?"((?:[^"\\]|\\.)*)"', arg)
    return m.group(1) if m else None


def event(seq: int, m: re.Match[str], dll: str, func: str, args: list[str],
          caller_va: str | None) -> Record:
    return {"seq": seq, "ts": float(m["ts"]), "tid": m["tid"], "dll": dll, "func": func,
            "args": args, "ret": None, "caller_va": caller_va}


def filter_log(src: Iterable[str]) -> list[Record]:
    stacks: dict[str, list[Record]] = {}  # calls waiting for their Ret line, per tid
    handles: dict[str, str] = {}  # file handle -> path
    records: list[Record] = []
    for line in src:
        line = line.rstrip("\n")
        if m := RE_CALL.match(line):
            ra = int(m["ra"], 16)
            if not EXE_LO <= ra < EXE_HI:
                continue
            args = split_args(m["args"])
            rec = event(len(records), m, m["dll"].lower(), m["func"], args, "%08X" % ra)
            if m["func"] in FILE_FUNCS and args and args[0].lower() in handles:
                rec["file"] = handles[args[0].lower()]
            records.append(rec)
            stacks.setdefault(m["tid"], []).append(rec)
        elif m := RE_RET.match(line):
            st = stacks.get(m["tid"], [])
            ra = "%08X" % int(m["ra"], 16)
            for k in range(len(st) - 1, -1, -1):
                r = st[k]
                if r["func"] == m["func"] and r["caller_va"] == ra:
                    r["ret"] = m["rv"]
                    del st[k:]
                    path = quoted(r["args"][0]) if r["func"].startswith("CreateFile") and r["args"] else None
                    if path is not None:
                        handles["%08x" % int(m["rv"], 16)] = path
                    break
        elif m := RE_D3D9.search(line):
            if m["func"] in D3D9_KEEP:
                records.append(event(len(records), m, "d3d9.com", m["func"], [m["rest"]], None))
        elif m := RE_SEH.match(line):
            records.append(event(len(records), m, "seh", m["code"], [m["name"] or "", m["addr"]], m["addr"]))
        elif m := RE_TNAME.match(line):
            records.append(event(len(records), m, "thread", "name", [m["id"], m["name"]], None))
        elif m := RE_LOAD.match(line):
            records.append(event(len(records), m, "loaddll", "load", [m["what"]], None))
    # Wine's helper processes log DLL loads too; keep only threads that made
    # an exe call, plus the thread names the exe set.
    exe_tids = {r["tid"] for r in records if r["dll"] not in ("loaddll", "thread")}
    return [r for r in records if r["tid"] in exe_tids or r["dll"] == "thread"]


def thread_names(recs: list[Record]) -> dict[str, str]:
    return {r["args"][0]: r["args"][1] for r in recs if r["dll"] == "thread"}


def milestones(recs: list[Record]) -> list[tuple[str, Record]]:
    """Rows (label, record) for the first-frame milestones."""
    out: list[tuple[str, Record]] = []

    def first(label: str, pred: Callable[[Record], bool]) -> None:
        r = next((r for r in recs if pred(r)), None)
        if r is not None:
            out.append((label, r))

    first("first call", lambda r: r["dll"] not in NOT_CALLS)
    first("first CreateWindowExA", lambda r: r["func"] == "CreateWindowExA")
    first("Direct3DCreate9", lambda r: r["func"] == "Direct3DCreate9")
    first("CreateDevice", lambda r: r["func"] == "d3d9_CreateDevice" and "iface" in r["args"][0])
    first("CreateDevice returned", lambda r: r["func"] == "d3d9_CreateDevice" and "Created" in r["args"][0])
    first("first Present", lambda r: r["dll"] == "d3d9.com" and "Present" in r["func"])
    first("first GAME.DAT open", lambda r: r["func"].startswith("CreateFile") and "GAME.DAT" in " ".join(r["args"]).upper())
    first("first ReadFile on GAME.DAT", lambda r: r["func"] == "ReadFile" and r.get("file", "").upper().endswith("GAME.DAT"))
    first("first unhandled-looking exception", lambda r: r["dll"] == "seh" and r["func"] == "c0000005")
    for i, r in enumerate(recs):
        if r["func"] == "CreateThread":
            # The exe names each thread right after it creates it (0x929E30).
            nxt = next((n for n in recs[i + 1:i + 400] if n["dll"] == "thread"), None)
            name = nxt["args"][1] if nxt else "?"
            out.append(("CreateThread %s (tid %s)" % (name, nxt["args"][0] if nxt else "?"), r))
    out.sort(key=lambda x: x[1]["seq"])
    return out


def write_summary(recs: list[Record], f: IO[str]) -> None:
    first_call = next((r for r in recs if r["dll"] not in NOT_CALLS), None)
    t0 = first_call["ts"] if first_call else 0.0
    main_tid = first_call["tid"] if first_call else ""
    presents = [r for r in recs if r["dll"] == "d3d9.com" and "Present" in r["func"]]
    names = thread_names(recs)
    f.write("| Milestone | seq | t (s) | tid | caller | detail |\n|---|---|---|---|---|---|\n")
    for label, r in milestones(recs):
        detail = ("%s(%s) -> %s" % (r["func"], ", ".join(r["args"]), r["ret"]))[:150]
        f.write("| %s | %d | %.3f | %s | %s | `%s` |\n" % (
            label, r["seq"], r["ts"] - t0, r["tid"], r["caller_va"] or "", detail.replace("|", "/")))
    f.write("\nPresent calls: %d" % len(presents))
    if presents:
        f.write(" (first at t=%.3f s, last at t=%.3f s)" % (presents[0]["ts"] - t0, presents[-1]["ts"] - t0))
    f.write("\n\nThreads in order of first event:\n\n| tid | name | events | first seq |\n|---|---|---|---|\n")
    counts: dict[str, list[int]] = {}
    for r in recs:
        if r["dll"] != "thread":
            c = counts.setdefault(r["tid"], [0, r["seq"]])
            c[0] += 1
    for t, (n, s) in counts.items():
        f.write("| %s | %s | %d | %d |\n" % (t, names.get(t, "main" if t == main_tid else ""), n, s))


def main() -> None:
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n")[0])
    ap.add_argument("log")
    ap.add_argument("out")
    ap.add_argument("--summary")
    a = ap.parse_args()
    with open(a.log, errors="replace") as src:
        recs = filter_log(src)
    with open(a.out, "w") as dst:
        for r in recs:
            dst.write(json.dumps(r) + "\n")
    print("%d records -> %s" % (len(recs), a.out), file=sys.stderr)
    if a.summary:
        with open(a.summary, "w") as f:
            write_summary(recs, f)


if __name__ == "__main__":
    main()
