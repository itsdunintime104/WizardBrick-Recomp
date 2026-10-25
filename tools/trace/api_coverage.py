"""Win32 API coverage: which calls in the Wine golden trace our runtime handles.

Usage:
    python -I tools/trace/api_coverage.py GOLDEN.jsonl [--src runtime/src]
        [--imports notes/win32_imports.md]

GOLDEN.jsonl: output of tools/trace/filter_relay.py (notes/golden_trace.md).

For every DLL!Func the original calls, the report gives the call count, first
seq, first time (s from the first exe call), threads, and our status, read from
the runtime's export tables (not guessed):
    impl      a slice entry that does real work
    partial   a slice entry that reports UNSUPPORTED on some inputs
    fixed     a slice entry that returns a constant and reads no argument
    stub      a slice entry that always reports UNSUPPORTED (or aborts)
    fallback  no slice entry; the generated stub in win32_unsupported.c binds
              (reports UNSUPPORTED once, returns 0)
    missing   no slice entry and not in the import inventory: an IAT import
              fails to resolve, and GetProcAddress returns 0
A second table lists every GetProcAddress call, with what Wine returned and
what our GetProcAddress (win32_core.c, import_lookup) would return.
"""

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass, field
from enum import StrEnum
from typing import Any

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from compare import ALIAS  # noqa: E402

# Wine forwards these exports to a differently named function; the relay log
# shows the target.
EXTRA_ALIAS = {"NtdllDefWindowProc_A": "DefWindowProcA", "RtlExitUserThread": "ExitThread"}
# Events that are not calls into a DLL.
NOT_CALLS = {"seh", "thread", "loaddll"}

RE_TABLE = re.compile(r"const\s+win32_export_t\s+(\w+)\[\]\s*=\s*\{(.*?)\n\};", re.S)
RE_ENTRY = re.compile(r'\{\s*"([^"]+)",\s*(?:"([^"]+)"|NULL),\s*(\d+)u?,\s*(\w+)\s*\}')
# An entry macro such as K32(name) {"KERNEL32.dll", #name, 0, imp_##name}.
RE_ENTRY_MACRO = re.compile(r'^#define\s+(\w+)\((\w+)\)\s*\{\s*"([^"]+)",\s*#\2,\s*(\d+)u?,\s*(\w*)##\s*\2\s*\}',
                            re.M)
RE_FUNC = re.compile(r"^(?:static\s+)?void\s+(\w+)\s*\(void\)[^\n{;]*\n?\s*\{", re.M)
RE_MACRO = re.compile(r"^#define\s+(\w+)\((\w+)[^)]*\)((?:.*\\\n)*.*)$", re.M)
RE_IMPORT_ROW = re.compile(r"^\|\s*([\w.]+\.dll)\s*\|\s*(\S+)\s*\|\s*(\S+)\s*\|", re.M | re.I)
RE_COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
RE_STRING = re.compile(r'"(?:\\.|[^"\\])*"')
RE_RETURN = re.compile(r"W32_RETURN\s*\(\s*\w+\s*,\s*([^;]*?)\)\s*;")
RE_CONST = re.compile(r"^\(?\s*(?:\(\w+\)\s*)?(?:0x[0-9a-fA-F]+u?|\d+u?|[A-Z][A-Z0-9_]*)\s*\)?$")


class Status(StrEnum):
    IMPL = "impl"
    PARTIAL = "partial"
    FIXED = "fixed"
    STUB = "stub"
    FALLBACK = "fallback"
    MISSING = "missing"
    COM = "com"


def stem(dll: str) -> str:
    d = dll.lower()
    return d[:-4] if d.endswith(".dll") else d


@dataclass
class Export:
    dll: str
    name: str | None
    ordinal: int
    fn: str
    table: str


@dataclass
class Runtime:
    slices: list[Export] = field(default_factory=list)
    inventory: dict[tuple[str, str], int] = field(default_factory=dict)  # (stem, name) -> ordinal
    bodies: dict[str, tuple[str, str]] = field(default_factory=dict)     # fn -> (file, body)

    def lookup(self, dll: str, name: str) -> tuple[Status, Export | None]:
        """Our import_lookup: slice by name, slice by inventory ordinal, else the
        generated stub when the import is in the inventory."""
        s = stem(dll)
        for e in self.slices:
            if stem(e.dll) == s and e.name == name:
                return Status.IMPL, e
        ordinal = self.inventory.get((s, name), 0)
        if ordinal:
            for e in self.slices:
                if stem(e.dll) == s and e.name is None and e.ordinal == ordinal:
                    return Status.IMPL, e
        if (s, name) in self.inventory:
            return Status.FALLBACK, None
        return Status.MISSING, None


def brace_body(text: str, open_at: int) -> str:
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_at + 1:i]
    return text[open_at + 1:]


def load_runtime(src: str, imports_md: str) -> Runtime:
    rt = Runtime()
    for fname in sorted(os.listdir(src)):
        if not fname.endswith(".c"):
            continue
        with open(os.path.join(src, fname), encoding="utf-8", errors="replace") as f:
            text = f.read()
        if fname != "win32_unsupported.c":
            macros = RE_ENTRY_MACRO.findall(text)
            for t in RE_TABLE.finditer(text):
                for m in RE_ENTRY.finditer(t.group(2)):
                    rt.slices.append(Export(m.group(1), m.group(2), int(m.group(3)), m.group(4),
                                            t.group(1)))
                for macro, _, dll, ordinal, prefix in macros:
                    for m in re.finditer(r"\b" + macro + r"\((\w+)\)", t.group(2)):
                        rt.slices.append(Export(dll, m.group(1), int(ordinal), prefix + m.group(1),
                                                t.group(1)))
        for m in RE_FUNC.finditer(text):
            rt.bodies.setdefault(m.group(1), (fname, brace_body(text, m.end() - 1)))
        # Functions defined by a macro such as D3DX_UNREACHED(name, nargs).
        for m in RE_MACRO.finditer(text):
            macro, param, body = m.group(1), m.group(2), m.group(3).replace("\\\n", "\n")
            proto = re.search(r"void\s+(\w*)##\s*" + param + r"\s*\(void\)", body)
            if proto is None:
                continue
            inner = body[body.find("{", proto.end()):]
            for inv in re.finditer(r"^" + macro + r"\((\w+)", text, re.M):
                rt.bodies.setdefault(proto.group(1) + inv.group(1), (fname, brace_body(inner, 0)))
    with open(imports_md, encoding="utf-8") as f:
        for m in RE_IMPORT_ROW.finditer(f.read()):
            ordinal = int(m.group(3)) if m.group(3).isdigit() else 0
            rt.inventory[(stem(m.group(1)), m.group(2))] = ordinal
    return rt


def classify_body(body: str) -> Status:
    raw = RE_COMMENT.sub("", body)
    code = RE_STRING.sub('""', raw)
    branches = re.search(r"\b(if|switch|for|while)\b|\?", code) is not None
    # An abort() behind a branch guards out-of-memory and the like; only an
    # UNSUPPORTED report marks inputs the slice does not handle.
    if re.search(r"\bdiag_unsupported\b", code) or "UNSUPPORTED" in raw:
        return Status.PARTIAL if branches else Status.STUB
    if re.search(r"\babort\s*\(", code) and not branches:
        return Status.STUB
    rets = RE_RETURN.findall(code)
    rest = RE_RETURN.sub("", code)
    if rets and not branches and "(" not in rest and "=" not in rest \
            and all(RE_CONST.match(v.strip()) for v in rets):
        return Status.FIXED
    return Status.IMPL


def status(rt: Runtime, dll: str, name: str) -> tuple[Status, str]:
    """(status, where) for dll!name; tries any DLL when Wine names a forward
    target (ntdll for kernel32, combase for ole32)."""
    kind, e = rt.lookup(dll, name)
    if kind == Status.MISSING:
        for other in sorted({stem(x.dll) for x in rt.slices} | {k[0] for k in rt.inventory}):
            k2, e2 = rt.lookup(other, name)
            if k2 != Status.MISSING:
                kind, e, dll = k2, e2, other
                break
    if e is None:
        return kind, stem(dll)
    body = rt.bodies.get(e.fn)
    where = f"{stem(e.dll)}:{e.fn}" + (f" ({body[0]})" if body else "")
    return (classify_body(body[1]) if body else Status.IMPL), where


def quoted(arg: str) -> str | None:
    m = re.search(r'"(.*)"', arg)
    return m.group(1) if m else None


@dataclass
class Row:
    count: int = 0
    first_seq: int = 0
    first_t: float = 0.0
    threads: set[str] = field(default_factory=set)
    rets: dict[str, int] = field(default_factory=dict)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("golden")
    ap.add_argument("--src", default="runtime/src")
    ap.add_argument("--imports", default="notes/win32_imports.md")
    a = ap.parse_args()
    rt = load_runtime(a.src, a.imports)

    rows: dict[tuple[str, str], Row] = {}
    names: dict[str, str] = {}
    modules: dict[str, str] = {}  # handle -> module name, from GetModuleHandle/LoadLibrary
    gpa: dict[tuple[str, str], Row] = {}
    t0: float | None = None
    with open(a.golden, encoding="utf-8") as f:
        for line in f:
            r: dict[str, Any] = json.loads(line)
            dll: str = r["dll"]
            func: str = r["func"]
            args: list[str] = r["args"]
            if dll == "thread" and func == "name":
                names.setdefault(args[0], args[1])
            if dll in NOT_CALLS:
                continue
            if t0 is None:
                t0 = r["ts"]
            func = EXTRA_ALIAS.get(func, ALIAS.get(func, func))
            row = rows.setdefault((dll, func), Row(first_seq=r["seq"], first_t=r["ts"] - t0))
            row.count += 1
            row.threads.add(r["tid"])
            ret = r["ret"] or "-"
            if len(row.rets) < 6 or ret in row.rets:
                row.rets[ret] = row.rets.get(ret, 0) + 1
            if func in ("GetModuleHandleA", "GetModuleHandleW", "LoadLibraryA", "LoadLibraryW",
                        "LoadLibraryExA", "LoadLibraryExW") and r["ret"] and args:
                mod = quoted(args[0])
                if mod is not None:
                    modules[r["ret"]] = mod
            if func == "GetProcAddress" and len(args) == 2:
                proc = quoted(args[1]) or "#" + args[1]
                g = gpa.setdefault((modules.get(args[0], args[0]), proc),
                                   Row(first_seq=r["seq"], first_t=r["ts"] - t0))
                g.count += 1
                g.rets[ret] = g.rets.get(ret, 0) + 1

    def thr(ts: set[str]) -> str:
        return ",".join(sorted(names.get(t, t) for t in ts))

    print("| DLL!Func | count | first seq | first t (s) | threads | status | ours | returns (value:count) |")
    print("|---|---|---|---|---|---|---|---|")
    totals: dict[Status, int] = {}
    for (dll, func), row in sorted(rows.items(), key=lambda kv: kv[1].first_seq):
        if dll == "d3d9.com":
            st, where = Status.COM, "gen_d3d9_guest.c"
        else:
            st, where = status(rt, dll, func)
        totals[st] = totals.get(st, 0) + 1
        rets = " ".join(f"{k}:{v}" for k, v in row.rets.items())
        print(f"| {dll}!{func} | {row.count} | {row.first_seq} | {row.first_t:.3f} | "
              f"{thr(row.threads)} | {st} | {where} | {rets} |")
    print()
    print("Totals: " + ", ".join(f"{k} {v}" for k, v in sorted(totals.items()))
          + f", APIs {sum(totals.values())}")
    print()
    print("GetProcAddress calls (module as the game named it):")
    print()
    print("| module!proc | count | first seq | Wine returns | ours |")
    print("|---|---|---|---|---|")
    for (mod, proc), g in sorted(gpa.items(), key=lambda kv: kv[1].first_seq):
        st, where = status(rt, mod, proc)
        ours = "0 (not found)" if st == Status.MISSING else f"{st} {where}"
        rets = " ".join(f"{k}:{v}" for k, v in g.rets.items())
        print(f"| {mod}!{proc} | {g.count} | {g.first_seq} | {rets} | {ours} |")


if __name__ == "__main__":
    main()
