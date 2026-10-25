"""Uber-shader permutation sweep over tools/shaders/sweep (the thunk path).

Sets per stage (vs_3_0 vertexProgram, ps_3_0 fragmentProgram, flags 0x200 as
the game passes for shader level 6): bare (no macros), base (the minimum the
builder always sets, see notes/shader_builder.md), base + each macro alone,
and 200 random base + 1..12 macro combinations (seed 1). Undeclared names
(E5005) are collected over up to 4 passes and declared as float4 stubs, the
stand-in for the builder's uniform block.

Usage: sweep.py HLSL_DIR SWEEP_BIN WORK_DIR [JOBS]
Writes WORK_DIR/results.tsv and prints the failure classes.
"""
import collections
import random
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_perm  # noqa: E402

STAGES = [('vs', 'vertexProgram', 'vs_3_0', 'BUILD_VERTEX_SHADER'),
          ('ps', 'fragmentProgram', 'ps_3_0', 'BUILD_PIXEL_SHADER')]
BASE = {'HLSL_LANGUAGE': '1', 'PIXEL_SHADER_VERSION': '30', 'VERTEX_SHADER_VERSION': '30',
        'RENDER_TARGET_COUNT': '1', 'LIGHTING_MODEL': '1'}
UNDECLARED = re.compile(r'E5005: Identifier "(\w+)" is not declared')


def sets(names: list[str]) -> list[tuple[str, dict[str, str]]]:
    out = [('bare', {}), ('base', dict(BASE))]
    out += [(f'one:{n}', {**BASE, n: BASE.get(n, '1')}) for n in names if n not in BASE]
    rng = random.Random(1)
    pool = [n for n in names if n not in BASE and not n.startswith('BUILD_')]
    for i in range(200):
        pick = rng.sample(pool, rng.randint(1, 12))
        out.append((f'rand:{i}', {**BASE, **{n: '1' for n in pick}}))
    return out


def run(hlsl: Path, binary: str, work: Path, jobs: int, perms, stubs: list[str]):
    srcdir = work / 'src'
    srcdir.mkdir(parents=True, exist_ok=True)
    (work / 'bin').mkdir(exist_ok=True)
    lines = []
    for i, (label, macros) in enumerate(perms):
        for st, entry, prof, build in STAGES:
            m = dict(macros) if label == 'bare' else {**macros, build: '1'}
            f = srcdir / f'{i}_{st}.hlsl'
            f.write_text(build_perm.build(hlsl, m, stubs), encoding='latin-1')
            lines.append((label, st, f'{f}\t{entry}\t{prof}\t200\t{work}/bin/{i}_{st}.bin\n'))
    chunks = [lines[k::jobs] for k in range(jobs)]
    procs = []
    for k, ch in enumerate(chunks):
        jf = work / f'jobs{k}.txt'
        jf.write_text(''.join(x[2] for x in ch))
        procs.append(subprocess.Popen([binary, str(jf)], stdout=subprocess.PIPE, text=True))
    results = []
    for ch, p in zip(chunks, procs):
        out, _ = p.communicate()
        cur = None
        for ln in out.splitlines():
            if ln.startswith('| '):
                assert cur is not None, 'detail line before any result line'
                cur[4].append(ln[2:])
                continue
            f = ln.split()
            label, st, _ = ch[int(f[0])]
            cur = [label, st, f[1], f[3] if f[1] == 'OK' else f[2], []]
            results.append(cur)
    return results


def classify(errs: list[str]) -> str:
    first = next((e for e in errs if ': E' in e), errs[0] if errs else 'no message')
    msg = re.sub(r'^.*?: (E\d+)', r'\1', first)
    return re.sub(r'"[^"]*"', '"X"', re.sub(r'\d+(\.\d+)?', 'N', msg))[:110]


def main() -> int:
    hlsl, binary, work = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
    jobs = int(sys.argv[4]) if len(sys.argv) > 4 else 8
    names = build_perm.macro_names(build_perm.inline(hlsl, 'ubershader.hlsl', set()))
    print(f'macro names in conditionals: {len(names)}')
    defined = set(build_perm.DEFINE.findall(build_perm.inline(hlsl, 'ubershader.hlsl', set())))
    perms = sets(names)
    stubs: list[str] = []
    res: list[list] = []
    for p in range(4):
        res = run(hlsl, binary, work, jobs, perms, stubs)
        new = sorted({m for r in res for e in r[4] for m in UNDECLARED.findall(e)} - set(stubs) - set(names) - defined - set(BASE))
        fails = sum(r[2] != 'OK' for r in res)
        print(f'pass {p}: compiles={len(res)} ok={len(res) - fails} fail={fails} new_undeclared={len(new)}')
        if not new:
            break
        stubs += new
    (work / 'stubs.txt').write_text('\n'.join(stubs) + '\n')
    with open(work / 'results.tsv', 'w') as f:
        for r in res:
            f.write('\t'.join([r[0], r[1], r[2], r[3], classify(r[4]) if r[2] != 'OK' else '']) + '\n')
    groups = collections.Counter((r[0].split(':')[0], r[1], r[2]) for r in res)
    for k in sorted(groups):
        print('  ', *k, groups[k])
    cls = collections.Counter(classify(r[4]) for r in res if r[2] != 'OK')
    for c, n in cls.most_common(25):
        print(f'{n:5d}  {c}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
