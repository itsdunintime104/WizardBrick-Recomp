"""Lift a PE32 image to C with the engine's lifter, without changing it.

Usage:
  python -I run_lift_pe.py --engine <HobbitRecomp/engine> --analysis <json> \
      --switch <json> --bin <exe> --out <dir> --prefix lhp [--jobs N] ...

The engine's tools/xrecomp/run.py reads the entry point by parsing the image
as a Mach-O. Everything else in the lift path is format-agnostic: the
translator only needs a virtual-address-to-file-offset map, which comes from
analysis.json. So this driver supplies the entry point from analysis.json and
hands the rest to the engine unchanged. When the PE front end moves into the
engine, this becomes configure_from_pe() plus an --format flag and this file
goes away.

It also names the MSVC frame helpers (__SEH_prolog4, __EH_prolog3*, and their
epilogs) for the engine's --seh-prolog/--seh-epilog, read by name from
names.json next to analysis.json. They build or tear down the caller's frame,
so the lifter must hand ebp across each call through g_seh_ebp; the engine's
byte-pattern detection only knows the older Xbox-era helpers.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


# __EH_epilog3_GS has no FID name in names.json. It is
# `mov ecx,[ebp-0x10]; xor ecx,ebp; call __security_check_cookie;
# jmp __EH_epilog3` (checked by disassembly).
UNNAMED_EPILOGS = (0x00A6EF57,)


def frame_helpers(names_path: Path) -> tuple[list[int], list[int]]:
    """(prolog VAs, epilog VAs) of the MSVC frame helpers, by name."""
    names = json.loads(names_path.read_text())
    prologs, epilogs = [], []
    for va, rec in names.items():
        name = rec["name"]
        if name == "__SEH_prolog4" or name.startswith("__EH_prolog3"):
            prologs.append(int(va, 16))
        elif name == "__SEH_epilog4" or name.startswith("__EH_epilog3"):
            epilogs.append(int(va, 16))
    return sorted(prologs), sorted({*epilogs, *UNNAMED_EPILOGS})


class _ImageEntry:
    """Stands in for the Mach-O header object, which only .entry is read from."""

    def __init__(self, entry: int) -> None:
        self.entry = entry


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--engine", type=Path, required=True)
    ap.add_argument("--analysis", type=Path, required=True)
    known, passthrough = ap.parse_known_args()

    sys.path.insert(0, str(known.engine.resolve()))
    from tools.hobbit import macho  # pyright: ignore[reportMissingImports]  (engine path is --engine)
    from tools.xrecomp import run as xrun  # pyright: ignore[reportMissingImports]

    entry = json.loads(known.analysis.read_text())["binary"]["entry"]
    macho.parse = lambda data: _ImageEntry(entry)  # type: ignore[assignment]

    prologs, epilogs = frame_helpers(known.analysis.parent / "names.json")
    seh = ["--seh-prolog", ",".join(f"0x{a:X}" for a in prologs),
           "--seh-epilog", ",".join(f"0x{a:X}" for a in epilogs)]
    # The CRT x87 math entries read ZF from their callers (notes/lift_review.md,
    # group A); recon/entry_zf.json holds that contract for the lifter.
    entry_zf = Path(__file__).resolve().parents[2] / "recon" / "entry_zf.json"
    if "--entry-zf" not in passthrough and entry_zf.exists():
        seh += ["--entry-zf", str(entry_zf)]
    sys.argv = [sys.argv[0], "--analysis", str(known.analysis), *seh,
                *passthrough]
    return xrun.main()


if __name__ == "__main__":
    raise SystemExit(main())
