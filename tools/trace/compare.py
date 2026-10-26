"""Compare our boot's import trace with the Wine golden trace.

Usage:
    python -I tools/trace/compare.py GOLDEN.jsonl OURS.log
        [--thread main|NAME|TID] [--context 8] [--ignore F1,F2] [--rets 5]

GOLDEN.jsonl: output of tools/trace/filter_relay.py.
OURS.log: stderr of tools/boot/run_boot.sh N HP_TRACE_IMPORTS=N. Two line
formats are accepted:
    IMP n Func(a0 a1 a2 a3) ret RET -> EAX                (pe_boot.c today)
    IMP n t=TID DLL!Func(a0 a1 ...) ret RET -> EAX        (spec, golden_trace.md)
    THR TID NAME                                          (spec: thread name)

The two sides are aligned per thread on the key (function, return address),
after dropping timing/lock calls (IGNORE) and collapsing runs of the same key.
The report gives the first divergence with context, the point where our trace
ends inside the golden one, and the first return-value differences inside
the matched part.
"""

import argparse
import difflib
import json
import re
from typing import Any

Record = dict[str, Any]

# Calls whose count or order depends on timing, not on program logic.
IGNORE = {"QueryPerformanceCounter", "GetTickCount", "timeGetTime", "Sleep", "SleepEx",
          "SwitchToThread", "EnterCriticalSection", "LeaveCriticalSection",
          "TryEnterCriticalSection", "InterlockedIncrement", "InterlockedDecrement",
          "InterlockedExchange", "InterlockedCompareExchange", "InterlockedExchangeAdd",
          "TlsGetValue", "TlsSetValue", "GetLastError", "SetLastError", "PeekMessageA",
          "PeekMessageW", "GetSystemTimeAsFileTime"}
# Wine never relays these (-norelay in kernel32.spec), so only our side has them.
NORELAY = {"GetCurrentProcessId", "GetCurrentThreadId", "GetCurrentProcess", "GetCurrentThread"}
# Wine relays some imports under the name of the export they forward to.
ALIAS = {"RtlInitializeCriticalSection": "InitializeCriticalSection",
         "RtlEnterCriticalSection": "EnterCriticalSection",
         "RtlLeaveCriticalSection": "LeaveCriticalSection",
         "RtlTryEnterCriticalSection": "TryEnterCriticalSection",
         "RtlDeleteCriticalSection": "DeleteCriticalSection",
         "RtlAllocateHeap": "HeapAlloc", "RtlReAllocateHeap": "HeapReAlloc",
         "RtlFreeHeap": "HeapFree", "RtlSizeHeap": "HeapSize",
         "RtlEncodePointer": "EncodePointer", "RtlDecodePointer": "DecodePointer",
         "RtlInitializeSListHead": "InitializeSListHead",
         "RtlInterlockedPushEntrySList": "InterlockedPushEntrySList",
         "RtlInterlockedPopEntrySList": "InterlockedPopEntrySList",
         "RtlInterlockedFlushSList": "InterlockedFlushSList",
         "RtlQueryDepthSList": "QueryDepthSList", "RtlCaptureContext": "RtlCaptureContext",
         "RtlUnwind": "RtlUnwind", "RtlZeroMemory": "RtlZeroMemory",
         "NtdllDefWindowProc_A": "DefWindowProcA", "NtdllDefWindowProc_W": "DefWindowProcW",
         "NtdllDialogWndProc_A": "DefDlgProcA",
         "RtlInitializeConditionVariable": "InitializeConditionVariable",
         "RtlWakeConditionVariable": "WakeConditionVariable",
         "RtlWakeAllConditionVariable": "WakeAllConditionVariable",
         "RtlInitializeSRWLock": "InitializeSRWLock",
         "RtlAcquireSRWLockExclusive": "AcquireSRWLockExclusive",
         "RtlReleaseSRWLockExclusive": "ReleaseSRWLockExclusive"}
NOT_CALLS = {"seh", "thread", "loaddll", "d3d9.com"}
RE_IMP = re.compile(r"^IMP (?P<n>\d+) (?:t=(?P<tid>\S+) )?(?:(?P<dll>[\w.]+)!)?(?P<func>[\w#@?$.]+)"
                    r"\((?P<args>[^)]*)\) ret (?P<ra>[0-9A-Fa-f]+) -> (?P<rv>[0-9A-Fa-f]+)")
RE_THR = re.compile(r"^THR (?P<tid>\S+) (?P<name>.*)$")


def norm(func: str) -> str:
    return ALIAS.get(func, func)


def load_golden(path: str) -> list[Record]:
    with open(path) as f:
        return [json.loads(line) for line in f]


def load_ours(path: str) -> tuple[list[Record], dict[str, str]]:
    recs: list[Record] = []
    names: dict[str, str] = {}
    with open(path, errors="replace") as f:
        for line in f:
            if m := RE_IMP.match(line):
                recs.append({"seq": int(m["n"]), "tid": m["tid"] or "", "dll": (m["dll"] or "").lower(),
                             "func": m["func"], "args": m["args"].split(), "ret": m["rv"].upper(),
                             "caller_va": m["ra"].upper().zfill(8)})
            elif m := RE_THR.match(line):
                names[m["tid"]] = m["name"].strip()
    # n is taken at call entry and the line is printed on return, so sort by n
    # to get call order (Wine logs at entry).
    recs.sort(key=lambda r: r["seq"])
    return recs, names


def pick_thread(recs: list[Record], names: dict[str, str], want: str) -> str | None:
    """tid for `want`: 'main' (first thread), a thread name, or a tid."""
    tids = [r["tid"] for r in recs]
    if not tids:
        return None
    if want == "main":
        return tids[0]
    by_name = [t for t, n in names.items() if n == want]
    if by_name:
        return by_name[0]
    return want if want in tids else None


def key(r: Record) -> tuple[str, str]:
    return norm(r["func"]), r["caller_va"]


def reduce(recs: list[Record], ignore: set[str]) -> list[tuple[Record, int]]:
    """Drop ignored calls and collapse runs of one key into (first, count)."""
    out: list[tuple[Record, int]] = []
    for r in recs:
        if norm(r["func"]) in ignore:
            continue
        if out and key(out[-1][0]) == key(r):
            out[-1] = (out[-1][0], out[-1][1] + 1)
        else:
            out.append((r, 1))
    return out


def fmt(item: tuple[Record, int]) -> str:
    r, n = item
    args = ", ".join(a[:40] for a in r["args"][:6])
    rep = " x%d" % n if n > 1 else ""
    return "#%-7d %s %-28s(%s) -> %s%s" % (r["seq"], r["caller_va"], norm(r["func"]), args, r["ret"], rep)


def small(v: str | None) -> int | None:
    if v is None:
        return None
    x = int(v, 16)
    return x if x < 0x10000 else None


def ret_differs(g: Record, o: Record) -> bool:
    """True when a return value is 0 on one side only, or two small values differ."""
    if g["ret"] is None or o["ret"] is None:
        return False
    gv, ov = int(g["ret"], 16), int(o["ret"], 16)
    if (gv == 0) != (ov == 0):
        return True
    sg, so = small(g["ret"]), small(o["ret"])
    return sg is not None and so is not None and sg != so


def compare(golden: list[Record], ours: list[Record], ctx: int, nrets: int) -> None:
    g, o = reduce(golden, IGNORE_SET), reduce(ours, IGNORE_SET)
    # Our trace is the shorter one; bound the golden side so the matcher stays fast.
    g = g[:len(o) * 3 + 2000]
    sm = difflib.SequenceMatcher(None, [key(x[0]) for x in g], [key(x[0]) for x in o], autojunk=False)
    ops = sm.get_opcodes()
    print("golden %d events, ours %d events (after ignore + collapse)" % (len(g), len(o)))
    matched = sum(i2 - i1 for tag, i1, i2, _, _ in ops if tag == "equal")
    print("matched %d of ours\n" % matched)

    rets = []
    for tag, i1, i2, j1, j2 in ops:
        if tag != "equal":
            continue
        for a, b in zip(g[i1:i2], o[j1:j2]):
            if ret_differs(a[0], b[0]):
                rets.append((a, b))
    print("== First return-value differences in matched calls (void calls show junk) ==")
    for a, b in rets[:nrets]:
        print("  golden " + fmt(a))
        print("  ours   " + fmt(b))
    if not rets:
        print("  none")
    print()

    first = next(((t, i1, i2, j1, j2) for t, i1, i2, j1, j2 in ops if t != "equal"), None)
    if first is None:
        print("No divergence: ours matches the golden prefix.")
        return
    tag, i1, i2, j1, j2 = first
    if j1 == len(o):
        print("== Ours ends after golden event %s; no divergence before that ==" %
              (fmt(g[i1 - 1]) if i1 else "start"))
        print("Golden continues with:")
        for x in g[i1:i1 + ctx]:
            print("  " + fmt(x))
        return
    print("== First divergence (%s): golden [%d:%d] vs ours [%d:%d] ==" % (tag, i1, i2, j1, j2))
    print("Common context:")
    for x in o[max(0, j1 - ctx):j1]:
        print("  " + fmt(x))
    print("Golden next:")
    for x in g[i1:i1 + ctx]:
        print("  " + fmt(x))
    print("Ours next:")
    for x in o[j1:j1 + ctx]:
        print("  " + fmt(x))
    same_site = i1 < len(g) and j1 < len(o) and g[i1][0]["caller_va"] == o[j1][0]["caller_va"]
    if same_site:
        print("\nSame call site, different function name: an alias gap (add to ALIAS) or an IAT binding bug.")


IGNORE_SET: set[str] = IGNORE | NORELAY


def main() -> None:
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n")[0])
    ap.add_argument("golden")
    ap.add_argument("ours")
    ap.add_argument("--thread", default="main", help="main, a thread name (Render, m_readThread ...) or a tid")
    ap.add_argument("--context", type=int, default=8)
    ap.add_argument("--ignore", default="", help="extra function names to drop, comma separated")
    ap.add_argument("--keep", default="", help="names to remove from the default ignore list")
    ap.add_argument("--rets", type=int, default=5, help="how many return-value differences to show")
    a = ap.parse_args()
    IGNORE_SET.update(x for x in a.ignore.split(",") if x)
    IGNORE_SET.difference_update(x for x in a.keep.split(",") if x)

    golden = [r for r in load_golden(a.golden) if r["dll"] not in NOT_CALLS]
    gnames = {}
    for r in load_golden(a.golden):
        if r["dll"] == "thread":
            gnames[r["args"][0]] = r["args"][1]
    gtid = pick_thread(golden, gnames, a.thread)
    ours, onames = load_ours(a.ours)
    has_tid = any(r["tid"] for r in ours)
    otid = pick_thread(ours, onames, a.thread) if has_tid else ""
    if gtid is None or otid is None:
        raise SystemExit("thread %r not found (golden %s, ours %s)" % (a.thread, gtid, otid))
    print("thread %s: golden tid %s, ours tid %s%s" % (
        a.thread, gtid, otid or "-", "" if has_tid else " (our log has no tids; using all lines)"))
    compare([r for r in golden if r["tid"] == gtid], [r for r in ours if r["tid"] == otid],
            a.context, a.rets)


if __name__ == "__main__":
    main()
