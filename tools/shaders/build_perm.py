"""Builds the HLSL text for one uber-shader permutation.

Stand-in for the engine's HLSLShaderBuilder (notes/shader_builder.md). The
engine runs its own preprocessor over the uber source with the material's
macro set and passes the result to D3DXCompileShader with no D3DXMACRO array
and no ID3DXInclude. This script gives an equivalent input for the
conditionals: '#define NAME VALUE' lines, then the source with its includes
inlined, so vkd3d evaluates the same #if lines the builder would.

NOT reproduced (unknown until the builder's pragma handler 0x9b32b0 and the
material-to-macro setter, vtable slot 0x1c, are reversed): which nu2_declare
items the builder emits, their register and semantic assignment, the uniform
block at nu2_anchorPoint(uniform), and the SEMANTIC_* defines that replace
VertexShaderSemantics.fx. Here every nu2_declare item is declared, varyings
get TEXCOORDn in order, and names vkd3d reports as undeclared can be given as
float4 stub uniforms.

Usage: build_perm.py HLSL_DIR OUT_FILE [--stubs FILE] [NAME[=VALUE] ...]
"""
import re
import sys
from pathlib import Path

INCLUDE = re.compile(r'^\s*#include\s+"([^"]+)"', re.M)
DECLARE = re.compile(r'#pragma nu2_declare\(\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*\)')
COND = re.compile(r'^\s*#\s*(?:if|ifdef|ifndef|elif)\b(.*)$', re.M)
WORD = re.compile(r'[A-Za-z_]\w*')
SEMANTIC = {'semantic_position': 'POSITION', 'semantic_normal': 'NORMAL',
            'semantic_tangent': 'TANGENT', 'semantic_bitangent': 'BINORMAL'}
NOT_MACROS = {'defined'}


def inline(hlsl_dir: Path, name: str, seen: set[str]) -> str:
    """The file with its #include lines replaced by the included text (once each)."""
    path = next((p for p in hlsl_dir.iterdir() if p.name.lower() == name.lower()), None)
    if path is None or path.name.lower() in seen:
        return ''  # VertexShaderSemantics.fx is not in the exe: the builder emits it
    seen.add(path.name.lower())
    text = path.read_text(encoding='latin-1')
    return INCLUDE.sub(lambda m: inline(hlsl_dir, m.group(1), seen), text)


DEFINE = re.compile(r'^\s*#\s*define\s+(\w+)', re.M)


def macro_names(src: str) -> list[str]:
    """Names used in #if/#ifdef/#ifndef/#elif lines and never #defined in the
    source (those are value names such as LIGHTING_BRDF_PHONG), in first-use
    order: the names only the builder can set."""
    names: dict[str, None] = {}
    local = set(DEFINE.findall(src))
    for m in COND.finditer(src):
        for w in WORD.findall(m.group(1).split('//')[0]):
            if w not in NOT_MACROS and w not in local:
                names[w] = None
    return list(names)


def declarations(src: str, stubs: list[str]) -> str:
    vin, vout, locs, seen, tc = [], [], [], set(), 0
    for kind, typ, name, sem in DECLARE.findall(src):
        if name in seen:
            continue
        seen.add(name)
        if kind == 'attribute':
            s = SEMANTIC.get(sem) or sem.replace('semantic_texcoord', 'TEXCOORD').replace(
                'semantic_color', 'COLOR').replace('semantic_', 'ATTR').upper()
            vin.append(f'{typ} {name} : {s};')
        elif kind == 'varying':
            locs.append(f'static {typ} {name};')
            if sem == 'semantic_position':
                vout.append(f'{typ} {name} : POSITION;')
            else:
                vout.append(f'{typ} {name} : TEXCOORD{tc};')
                tc += 1
        else:
            locs.append(f'static {typ} {name};')
    out = ['struct VertexInput {', *vin, '};', 'struct VertexOutput {', *vout, '};',
           'struct FragmentOutput { float4 color[RENDER_TARGET_COUNT] : COLOR0; };',
           'static VertexInput vin; static VertexOutput vout; static FragmentOutput fout;', *locs]
    for s in stubs:
        if s not in seen:
            out.append(f'uniform {"float4x4" if re.match("(world|viewProj|view|proj)", s) else "float4"} {s};')
    return '\n'.join(out) + '\n'


def build(hlsl_dir: Path, macros: dict[str, str], stubs: list[str]) -> str:
    src = inline(hlsl_dir, 'ubershader.hlsl', set())
    defines = ''.join(f'#define {k} {v}\n' for k, v in macros.items())
    return defines + declarations(src, stubs) + '#line 1\n' + src


def main() -> int:
    args = sys.argv[1:]
    if len(args) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    hlsl_dir, out = Path(args[0]), Path(args[1])
    stubs: list[str] = []
    rest = args[2:]
    if rest[:1] == ['--stubs']:
        stubs = Path(rest[1]).read_text().split()
        rest = rest[2:]
    macros = dict((a.split('=', 1) + ['1'])[:2] for a in rest)
    out.write_text(build(hlsl_dir, macros, stubs), encoding='latin-1')
    return 0


if __name__ == '__main__':
    sys.exit(main())
