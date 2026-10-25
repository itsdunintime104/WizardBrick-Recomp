"""Write filetable.txt for the game's shader dump routine (sub_00A3C660).

The routine reads one quoted game path per line and loads each path that
contains `_nxg.gsc` or `_nxg.ghg` (notes/shader_oracle.md, "Why -DumpShaders
does not work"). The paths come from the name tables of the user's GAME*.DAT
archives (notes/assets.md section 2).

Usage: python3 -I tools/shaders/scene_list.py <game dir> <out filetable.txt>
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

SUFFIXES = ("_nxg.gsc", "_nxg.ghg")


def dat_paths(data: bytes) -> list[str]:
    """Every file path in one GAME*.DAT, from its name-node tree."""
    table_off, table_size = struct.unpack_from("<II", data, 0)
    t = data[table_off:table_off + table_size]
    count = struct.unpack_from("<I", t, 4)[0]
    p = 8 + 16 * count
    n_nodes = struct.unpack_from("<I", t, p)[0]
    p += 4
    # Node: i16 a, i16 prev, u32 name offset. Folder: a = its last child,
    # children chain back through prev (0 ends). File: a = minus the file id.
    nodes = [struct.unpack_from("<hhI", t, p + 8 * i) for i in range(n_nodes)]
    p += 8 * n_nodes
    blob_size = struct.unpack_from("<I", t, p)[0]
    blob = t[p + 4:p + 4 + blob_size]

    def name(off: int) -> str:
        return blob[off:blob.index(b"\0", off)].decode("latin1")

    out: list[str] = []
    stack: list[tuple[int, str]] = [(0, "")]
    while stack:
        i, prefix = stack.pop()
        a, _, off = nodes[i]
        full = f"{prefix}\\{name(off)}" if prefix else name(off)
        if i == 0 or (a > 0 and a != i):
            k, children = a, []
            while k > 0:
                children.append(k)
                k = nodes[k][1]
            stack.extend((c, full) for c in children)
        else:
            out.append(full)
    return out


def scene_paths(game_dir: Path) -> list[str]:
    """Distinct lower-case _nxg scene paths of all GAME*.DAT, sorted."""
    found: set[str] = set()
    for dat in sorted(game_dir.glob("GAME*.DAT")):
        with dat.open("rb") as f:
            head = f.read(8)
            table_off, table_size = struct.unpack("<II", head)
            f.seek(table_off)
            data = head + bytes(table_off - 8) + f.read(table_size)
        found.update(p.lower() for p in dat_paths(data) if p.lower().endswith(SUFFIXES))
    return sorted(found)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("usage: scene_list.py <game dir> <out filetable.txt>", file=sys.stderr)
        return 2
    paths = scene_paths(Path(argv[1]))
    if not paths:
        print(f"scene_list: no _nxg scenes in {argv[1]}/GAME*.DAT", file=sys.stderr)
        return 1
    Path(argv[2]).write_bytes("".join(f'"{p}"\r\n' for p in paths).encode("latin1"))
    print(f"scene_list: {len(paths)} scenes -> {argv[2]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
