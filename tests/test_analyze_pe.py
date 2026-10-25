"""find_functions on small hand-assembled i386 byte strings."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path

import pytest

_PATH = Path(__file__).resolve().parents[1] / "tools" / "pe" / "analyze_pe.py"
_SPEC = importlib.util.spec_from_file_location("analyze_pe", _PATH)
assert _SPEC is not None and _SPEC.loader is not None
A = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(A)

BASE = 0x1000


def spans(code: bytes, candidates: set[int],
          entries: set[int]) -> dict[int, int]:
    """{start: end} of the kept functions, with `entries` as listed starts."""
    found = A.find_functions(A.Code(code, BASE, BASE, BASE + len(code)),
                             candidates,
                             {va: A.Evidence.LISTED for va in entries},
                             frozenset(), frozenset())
    return {start: f.end for start, f in found.items()}


def test_loop_alignment_padding_merges_into_owner() -> None:
    code = bytes.fromhex(
        "8b442404"          # 1000 mov eax, [esp+4]
        "eb0a"              # 1004 jmp 1010
        "8da42400000000"    # 1006 lea esp, [esp]   (padding, false start)
        "8d4900"            # 100d lea ecx, [ecx]
        "48"                # 1010 dec eax          (loop head, false start)
        "75fd"              # 1011 jne 1010
        "c3")               # 1013 ret
    assert spans(code, {0x1000, 0x1006, 0x1010}, {0x1000}) == {0x1000: 0x1014}


def test_switch_label_is_not_kept() -> None:
    code = bytes.fromhex(
        "83f801"            # 1000 cmp eax, 1
        "7713"              # 1003 ja 1018
        "ff24850c100000"    # 1005 jmp [eax*4 + 100c]
        "14100000"          # 100c dd 1014
        "16100000"          # 1010 dd 1016
        "40c3"              # 1014 caseD_0: inc eax; ret
        "48c3"              # 1016 caseD_1: dec eax; ret
        "31c0c3")           # 1018 default: xor eax, eax; ret
    assert spans(code, {0x1000, 0x1014, 0x1016}, {0x1000}) == {0x1000: 0x101b}


def test_call_target_is_kept() -> None:
    code = bytes.fromhex(
        "e803000000"        # 1000 call 1008
        "c3"                # 1005 ret
        "cccc"              # 1006 int3 fill
        "31c0c3")           # 1008 xor eax, eax; ret (not a candidate)
    assert spans(code, {0x1000}, {0x1000}) == {0x1000: 0x1006,
                                                0x1008: 0x100b}


def test_backward_jump_is_inside_owner_extent() -> None:
    code = bytes.fromhex(
        "31c0"              # 1000 xor eax, eax
        "40"                # 1002 inc eax          (loop head)
        "83f805"            # 1003 cmp eax, 5
        "7403"              # 1006 je 100b
        "90"                # 1008 nop              (false start)
        "ebf7"              # 1009 jmp 1002         (backward)
        "c3")               # 100b ret
    assert spans(code, {0x1000, 0x1008}, {0x1000}) == {0x1000: 0x100c}


REPO = Path(__file__).resolve().parents[1]
EXE = REPO / "orig.nosync" / "lhp_win32.exe"


@pytest.fixture(scope="module")
def game_spans() -> dict[int, int]:
    """{start: end} of the function list of the game executable."""
    if not EXE.exists():
        pytest.skip("needs the game executable")
    result = A.analyze(
        EXE.read_bytes(),
        json.loads((REPO / "recon/ghidra_functions.json").read_text()),
        json.loads((REPO / "recon/ghidra_text_data.json").read_text()),
        A.read_starts(REPO / "recon/drop_function_starts.txt"),
        A.read_starts(REPO / "recon/add_function_starts.txt"),
        A.read_ranges(REPO / "recon/init_tables.txt"))
    return {f["addr"]: f["addr"] + f["size"] for f in result["functions"]}


def test_crt_initialisers_are_starts(game_spans: dict[int, int]) -> None:
    """Initialisers in __xc_a..__xc_z that Ghidra missed (registry read,
    semaphores, a static constructor) are kept as function starts."""
    assert {0xC3EAA8, 0xC3E8C0, 0xC3E3E0} <= game_spans.keys()


def test_hresult_switch_is_one_function(game_spans: dict[int, int]) -> None:
    """FUN_00aa86b6 (HRESULT to text: compare tree plus 26 jump tables)
    is one function. Its case labels and Ghidra's piece FUN_00ab0749 (a
    compare node after `jmp 0xAB45ED`) are not starts."""
    assert game_spans[0xAA86B6] > 0xAB45EC
    assert not {0xAB21E2, 0xAAFD17, 0xAB316F, 0xAB0749} & game_spans.keys()


def test_incremental_link_thunk_and_target_are_starts(
        game_spans: dict[int, int]) -> None:
    """The initializer 0xC3BCC0 calls the thunk 0x40366B (`jmp 0x5349A0`,
    the app constructor); both are starts (notes/boot_log.md failure 5)."""
    assert {0x40366B, 0x5349A0} <= game_spans.keys()
    assert game_spans[0x40366B] == 0x40366B + 5


def test_pushed_callback_after_fill_is_start(
        game_spans: dict[int, int]) -> None:
    """0x670040 is not a Ghidra function; `push 0x670040` at 0x6A57D9
    registers it as a callback and boot calls it (tmp/boot/run11.log)."""
    assert 0x670040 in game_spans


def test_function_tail_is_not_a_start(game_spans: dict[int, int]) -> None:
    """0x670036 (`mov eax, 1; pop esi; ret`) is the tail of 0x66FF60, which
    runs through 0x66FFFE (a data-dword false start) to 0x67003D."""
    assert not {0x66FFFE, 0x67000B, 0x670036} & game_spans.keys()
    assert game_spans[0x66FF60] == 0x67003D


def test_orphan_jump_into_uncovered_code_is_start(
        game_spans: dict[int, int]) -> None:
    """_acos 0xA693A0 jumps (`je`) to its x87 path 0xA6942F, which lies past
    the next function 0xA693E0 and in no other span."""
    assert 0xA6942F in game_spans


def test_shared_tails_are_starts(game_spans: dict[int, int]) -> None:
    """Code reached by a jump from another function becomes a start (a tail
    call): the SEH4 join 0xA6F46E, entered from 0xA6F449 and from its
    __except handler 0xA6F46B, and the x87 entry 0xA77738, two bytes into
    0xA77736."""
    assert {0xA6F46E, 0xA77738, 0xA6F449, 0xA6F46B} <= game_spans.keys()


def test_jump_table_decodes_are_not_starts(game_spans: dict[int, int]) -> None:
    """0x969B8E and 0xA8A7E3 run from `8b ff` into Ghidra pointer tables
    (the lcall and arpl sites); the switch 0x9080E0 is kept."""
    assert not {0x969B8E, 0xA8A7E3} & game_spans.keys()
    assert 0x9080E0 in game_spans
