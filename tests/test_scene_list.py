"""scene_list on a hand-built GAME.DAT name table."""

from __future__ import annotations

import importlib.util
import struct
from pathlib import Path

_PATH = Path(__file__).resolve().parents[1] / "tools" / "shaders" / "scene_list.py"
_SPEC = importlib.util.spec_from_file_location("scene_list", _PATH)
assert _SPEC is not None and _SPEC.loader is not None
S = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(S)


def make_dat(nodes: list[tuple[int, int, str]], pad: int = 5) -> bytes:
    """GAME.DAT bytes: header, `pad` bytes of members, then the name table."""
    blob, offs = b"", []
    for _, _, name in nodes:
        offs.append(len(blob))
        blob += name.encode("latin1") + b"\0"
    table = struct.pack("<iI", 1, 0) + struct.pack("<I", len(nodes))
    table += b"".join(struct.pack("<hhI", a, prev, o) for (a, prev, _), o in zip(nodes, offs))
    table += struct.pack("<I", len(blob)) + blob
    return struct.pack("<II", 8 + pad, len(table)) + bytes(pad) + table


# root -> levels -> {hub -> {hub_nxg.gsc, hub.txt}, Chars_NXG.GHG}
NODES = [
    (1, 0, ""),               # 0 root: last child 1
    (5, 0, "levels"),         # 1 folder: last child 5
    (4, 0, "hub"),            # 2 folder: last child 4
    (-1, 0, "hub_nxg.gsc"),   # 3 file 1
    (-2, 3, "hub.txt"),       # 4 file 2, previous sibling 3
    (-3, 2, "Chars_NXG.GHG"),  # 5 file 3, previous sibling 2
]


def test_dat_paths_walks_the_tree() -> None:
    assert sorted(S.dat_paths(make_dat(NODES))) == [
        "levels\\Chars_NXG.GHG", "levels\\hub\\hub.txt", "levels\\hub\\hub_nxg.gsc"]


def test_main_writes_quoted_scene_lines(tmp_path: Path) -> None:
    (tmp_path / "GAME.DAT").write_bytes(make_dat(NODES))
    (tmp_path / "GAME1.DAT").write_bytes(make_dat(NODES, pad=9))  # same paths: one line each
    out = tmp_path / "filetable.txt"
    assert S.main(["scene_list.py", str(tmp_path), str(out)]) == 0
    assert out.read_bytes() == b'"levels\\chars_nxg.ghg"\r\n"levels\\hub\\hub_nxg.gsc"\r\n'


def test_main_fails_without_scenes(tmp_path: Path) -> None:
    assert S.main(["scene_list.py", str(tmp_path), str(tmp_path / "f.txt")]) == 1
