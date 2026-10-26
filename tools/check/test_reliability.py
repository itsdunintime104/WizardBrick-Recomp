"""Unit tests for tools/check/reliability.py: pytest tools/check (repo .venv, as engine/windows/tests/run_all.sh)."""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import reliability as r  # noqa: E402


def steady(t0_ms: float, t1_ms: float, dt: float, n0: int = 1) -> list[r.Frame]:
    out, t, n = [], t0_ms, n0
    while t < t1_ms:
        t += dt
        out.append((n, t, dt))
        n += 1
    return out


def test_input_script_is_wall_clock_and_pairs_pauses() -> None:
    lines = [ln.split() for ln in r.input_script(600).splitlines() if not ln.startswith("#")]
    pause = [(int(ms), key) for ms, act, key in lines
             if key in ("RETURN", "ESCAPE") and int(ms) >= r.PAUSE_AT[0] * 1000]
    assert pause == [x for t in r.PAUSE_AT for x in ((t * 1000, "ESCAPE"), ((t + r.PAUSE_HOLD_S) * 1000, "RETURN"))]
    assert all(key == "RETURN" for ms, act, key in lines if int(ms) <= 60_000)
    assert max(int(ms) for ms, _, _ in lines) <= (600 - 20) * 1000
    assert all(int(ms) < r.COVER_AT * 1000 or int(ms) >= r.PLAY2_AT * 1000 for ms, _, _ in lines)


def test_key_events_take_the_present_before_them() -> None:
    log = "D3D9 Present #10 hr=00000000\nINPUT t=440000 down RETURN\nD3D9 Present #11 hr=00000000\n"
    assert r.key_events(log) == [(440000, "down", "RETURN", 10)]


def test_pause_spans_pair_the_open_and_close_keys() -> None:
    events = [(30_000, "down", "RETURN", 1), (440_000, "down", "ESCAPE", 10), (440_000, "up", "ESCAPE", 11),
              (446_000, "down", "RETURN", 20)]
    assert r.pause_spans(events, {1: 1.0, 10: 100.0, 11: 110.0, 20: 200.0}) == [(100.0, 200.0)]


def test_open_ms_ends_at_the_last_stall_after_the_key() -> None:
    frames = steady(0, 1000, 16.7) + [(1000, 1500.0, 300.0)] + steady(1500, 3000, 16.7, 1001)
    times = [t for _, t, _ in frames]
    slow, quick = r.open_ms(frames, times, 1000.0 - 16.7 * 2, set()), r.open_ms(frames, times, 2000.0, set())
    assert slow is not None and abs(slow - (500 + 16.7 * 2)) < 20
    assert quick is not None and quick < 20


def test_dump_presents_are_not_stalls() -> None:
    # The run's artefact: the only stall after the key is a frame-dump Present (85 ms, 3.9 s later).
    frames = steady(0, 1000, 8.3) + steady(1000, 4815, 8.3, 200) + [(600, 4900.0, 85.0)] + steady(4900, 6000, 8.3, 601)
    times = [t for _, t, _ in frames]
    with_dump = r.open_ms(frames, times, 1000.0, set())
    without = r.open_ms(frames, times, 1000.0, {600})
    assert with_dump is not None and with_dump > 3800
    assert without is not None and without < 20
    assert r.load_stalls(frames, 10_000, [], set()) == [(4815.0, 4900.0)]
    assert r.load_stalls(frames, 10_000, [], {600}) == []


def test_load_stalls_merge_close_stalls_and_skip_samples() -> None:
    frames = steady(0, 1000, 16.7) + [(100, 1200.0, 200.0), (101, 1500.0, 300.0)] + steady(1500, 5000, 16.7, 102)
    frames += [(500, 5600.0, 600.0)]
    loads = r.load_stalls(frames, 10_000, [[9.0, 10.0]], set())  # padded to 4-15 s: drops the 5.6 s stall
    assert loads == [(1000.0, 1500.0)]


GOLD, LIGHT_BLUE, GRASS = (220, 170, 40), (100, 190, 240), (120, 140, 90)


def ppm(path: Path, boxes: dict[tuple[int, int, int, int], tuple[int, int, int]], w: int = 96, h: int = 60) -> Path:
    """A w x h dump of GRASS with each box (960x600 coordinates) filled with its colour."""
    px = bytearray(bytes(GRASS) * (w * h))
    for (x0, y0, x1, y1), rgb in boxes.items():
        for y in range(int(y0 * h / r.REF_H), int(y1 * h / r.REF_H)):
            for x in range(int(x0 * w / r.REF_W), int(x1 * w / r.REF_W)):
                px[(y * w + x) * 3:(y * w + x) * 3 + 3] = bytes(rgb)
    path.write_bytes(f"P6\n{w} {h}\n255\n".encode() + bytes(px))
    return path


def test_menu_score_needs_the_label_and_the_column(tmp_path: Path) -> None:
    menu = ppm(tmp_path / "menu.ppm", {r.MENU_LABEL_BOX: GOLD, r.MENU_COL_BOX: LIGHT_BLUE})
    # Gameplay with a blue spell in the column box but no gold "Paused" label is not the menu.
    game = ppm(tmp_path / "game.ppm", {r.MENU_COL_BOX: LIGHT_BLUE})
    assert r.menu_score(menu)[2] is True
    assert r.menu_score(game)[2] is False


def test_pause_needs_a_dump_that_shows_the_menu(tmp_path: Path) -> None:
    game = ppm(tmp_path / "a.ppm", {r.MENU_COL_BOX: LIGHT_BLUE})
    menu = ppm(tmp_path / "b.ppm", {r.MENU_LABEL_BOX: GOLD, r.MENU_COL_BOX: LIGHT_BLUE})
    t_of = {300: 9_000.0, 600: 12_000.0}
    frames = steady(8_000, 16_000, 16.7, 1)
    times = [t for _, t, _ in frames]
    opened = r.pause_result(frames, times, [(300, game), (600, menu)], t_of, 10_000.0, 16_000.0)
    assert isinstance(opened, float) and opened < 20
    assert r.pause_result(frames, times, [(300, game), (600, game)], t_of, 10_000.0, 16_000.0) == "pause not opened"
    # A dump at key + 0.2 s is before MENU_DELAY_S: it proves nothing.
    assert r.pause_result(frames, times, [(300, game), (600, menu)], {300: 9_000.0, 600: 10_200.0},
                          10_000.0, 16_000.0) == "no dump in hold"


def test_a_dump_lands_in_every_pause_hold_at_60_fps() -> None:
    assert r.DUMP_STEP / r.FPS_TARGET <= r.PAUSE_HOLD_S - r.MENU_DELAY_S
    assert all(b - a > r.PAUSE_HOLD_S for a, b in zip(r.PAUSE_AT, (*r.PAUSE_AT[1:], r.COVER_AT)))


def test_histogram_shows_60_on_120_judder() -> None:
    smooth = r.histogram([16.67] * 100, 120)
    judder = r.histogram([8.33, 25.0] * 50, 120)
    assert smooth == [0, 100, 0, 0] and r.judder_pct(smooth) == 0
    assert judder == [50, 0, 50, 0] and r.judder_pct(judder) == 50


def test_fps_ignores_the_partial_first_second(tmp_path: Path) -> None:
    (tmp_path / "run.json").write_text(json.dumps({
        "exited_early": False, "exit_code": None, "samples": {}, "rss": [], "new_crash_reports": [],
        "cover": None, "ft_offset_s": None}))
    (tmp_path / "game.log").write_text("")
    # 60 fps from 0 to 20 s; boot_t comes from no dump, so judge counts from SETTLE_S (10 s).
    (tmp_path / "ft.txt").write_text("".join(f"F {n} {t:.3f} {dt:.3f} 0 0 0 - 0\n"
                                             for n, t, dt in steady(0, 20_000, 1000 / 60)))
    res, m = r.judge(tmp_path, 600, None, r.BOOT_LIMIT_S)
    assert m["boot_s"] is None and m["fps_min"] is None
    per = [x for x, _, _ in res if x.name == "fps min"][0]
    assert per.detail.startswith(("59", "60"))


def test_compare_rules() -> None:
    assert r.worse("pause_open_ms", 400.0, 700.0) == (True, "<= 600")
    assert r.worse("pause_open_ms", 400.0, 590.0)[0] is False
    assert r.worse("cover_resume_ms", None, 1500.0)[0] is True        # no baseline: absolute 1 s
    assert r.worse("fps_median", 115.0, 60.0)[0] is False             # capped at the 60 target
    assert r.worse("fps_median", 60.0, 50.0)[0] is True
    assert r.worse("judder_play", 1.0, 7.0)[0] is True
    assert r.worse("hidden_stuck", None, True)[0] is True
    assert r.worse("boot_s", 280.0, 360.0)[0] is None
    assert r.worse("load_ms", 300.0, None) == (True, "candidate missing")


def test_warm_runs_first_into_warm_and_judges_the_second(tmp_path: Path, monkeypatch) -> None:
    outs: list[Path] = []
    monkeypatch.setattr(r, "run", lambda a: outs.append(a.out))
    monkeypatch.setattr(r, "judge", lambda out, *_: ([], {"judged": str(out)}))
    monkeypatch.setattr(r, "print_table", lambda res, out: 0)
    assert r.main(["run", "--exe", "x.app", "--seconds", "600", "--out", str(tmp_path), "--warm"]) == 0
    assert outs == [tmp_path / "warm", tmp_path]
    assert json.loads((tmp_path / "metrics.json").read_text()) == {"judged": str(tmp_path)}
    outs.clear()
    r.main(["run", "--exe", "x.app", "--seconds", "600", "--out", str(tmp_path)])
    assert outs == [tmp_path]
