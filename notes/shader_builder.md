# HLSLShaderBuilder and the uber-shader sweep (measured, partial)

Date: 2026-10-24. Static read of `orig.nosync/lhp_win32.exe` (capstone, nothing executed). Scratch: `<job tmp>/shader_perms/` (`d.py` annotated disassembler, `a04e20.txt`, `a06af0.txt`, `a05280.txt`, `run/results.tsv`, `run/stubs.txt`, `tm/` timings).

## Call chain (confirmed)

| VA | Role | Evidence |
|---|---|---|
| 0xa06af0 | build + create one shader pair (thiscall, ecx = shader manager, arg0 = material desc) | `call 0x9a8ae0` (key from desc), `call 0xa05280`, `call 0xa04e20`; QueryPerformanceCounter-style timer 0x929f90 around each |
| 0xa05280 | builder entry, `ecx = [mgr+0x37c94c]` (the HLSLShaderBuilder), args (desc, &vs, &ps) | outputs: VS text ptr/len at mgr+0x37c950/+0x37c954, PS text ptr/len at mgr+0x37c960/+0x37c964 (0xa06b36..0xa06be5) |
| 0xa04e20 | cache lookup 0x9f6100, else D3DXCompileShader x2 | arg2/arg3 = VS text/len, arg4/arg5 = PS text/len, arg6 = shader level (`[builder+0x1060]`) |

D3DXCompileShader arguments (0xa04fe3..0xa0506b): `pDefines = 0`, `pInclude = 0`, entry `vertexProgram` (0xd1c22c) / `fragmentProgram` (0xd1c21c), profile from the level table (level 6: `vs_3_0`/`ps_3_0`), flags 0x200 (level >= 3) or 0x10200. So the generated text is self-contained: every macro and every include is resolved by the builder, not by D3DX.

## Builder internals (partly read)

- The builder runs its own preprocessor. Directive dispatcher around 0xa05d30: `define` (0xd1c304), `ifdef`, `ifndef`, `undef`, `include` (0xc74b14), `pragma`, `if` (0xd1c2e0), `else`, `elif`, `endif`. Handlers: define 0x996760, pragma 0x9b32b0, if 0x996ca0 (expression) + 0x996880, else 0x996920, elif 0x996ca0 + 0x996940, endif 0x9969e0.
- Macro table at builder+0x20c: entries of 12 bytes {name, value, flag}; count at +0x578. `_SHADERBUILDER_DEBUG` (0xd1c2b0) is inserted with value 0 and flag 0 (0xa05354..0xa053a8), so the debug block (FragmentOutput, `STATIC vin/vout/fout`, the five anchors) is inactive in real builds.
- The material macros come from a virtual call, vtable slot 0x1c of the builder (0xa0531c), with the desc and the macro table.
- Uniform reservation: `uniform float4 reserve[%d] : c0;` (0xd1c28c) sized from the max register end over the global name table 0xfc1cdc..0xfe1864 (stride 0x368) (0xa053be..0xa05410).

NOT reversed (next step): the pragma handler 0x9b32b0 (what `nu2_declare` / `nu2_use` / `nu2_anchorPoint` emit, register and semantic assignment), the slot 0x1c macro setter (material flags to macros), and the text emitter. Until then `tools/shaders/build_perm.py` is a stand-in, not a reproduction.

## Sweep (stand-in builder, vs_3_0 + ps_3_0, flags 0x200, through thunk_D3DXCompileShader)

`tools/shaders/sweep.py HLSL_DIR SWEEP_BIN WORK [JOBS]`; harness `tools/shaders/sweep.c`. 244 macro names appear in conditionals and are never `#define`d by the source (the 183 of shader_pipeline.md used a different count). Base set: HLSL_LANGUAGE, PIXEL/VERTEX_SHADER_VERSION=30, RENDER_TARGET_COUNT=1, LIGHTING_MODEL=1, BUILD_*_SHADER. 91 undeclared names became float4 stubs.

| Set | Compiles | OK | Fail |
|---|---|---|---|
| bare (no macros) | 2 | 0 | 2 (undeclared identifiers: expected, the builder always defines) |
| base | 2 | 2 | 0 |
| base + one macro | 480 | 397 | 83 |
| base + 1..12 random macros (200 sets, seed 1) | 400 | 156 | 244 |
| total | 884 | 555 | 329 |
| (c) sets from material code | not run: needs the slot 0x1c setter | | |

Failure classes (count, likely cause):

| n | First error | Cause |
|---|---|---|
| 110 | `tex2D`/`texCUBE` argument is float4, expected sampler | stand-in: stubs are float4; real uniforms are samplers |
| 58 | Function not defined | stand-in or invalid macro combination; not checked per case |
| 50 | Infinities and NaNs are not allowed by the shader model | UNCONFIRMED: possible vkd3d constant-folding check that D3DX lacks; first seen with `one:HDR_PRESERVESATURATION` ps. Needs a minimal HLSL and the D3DX result |
| 40 | Can't cast float4 to float4x4 | stand-in: matrix uniform stubbed as float4 |
| 14 | Identifier already declared | stand-in declares every nu2_declare item |
| 14 | Invalid swizzle | stand-in types (half4 vs half3) |
| 10 | Struct already defined | `_SHADERBUILDER_DEBUG` set by the sweep; the builder never sets it |
| 10 + 6 | Can't implicitly convert halfN/floatN to halfN/halfNxN | stand-in types |
| 8 | Identifier not declared | stub set incomplete in random sets |
| 6 | Array index out of bounds | `NORMAL_OUTPUT` with RENDER_TARGET_COUNT=1: define-set artefact |
| 3 | Statement modifies a const expression | not classified |

No failure was traced to `runtime/src/d3dx_shader.c` glue. No vkd3d gap is confirmed; the NaN class is the one candidate.

## Disk cache (runtime/src/d3dx_shader.c)

Key: SHA-256 of a format string, `vkd3d_shader_get_version()`, `D3DX_COMPILER_REVISION` (bump with every local vkd3d patch), source bytes, macro names and values, entry, profile, flags. File `~/Library/Caches/HarryPotterRecomp/shaders/<hex>.bin`: magic, code size, message size, byte code (its CTAB comment is the constant table), messages. Written to a temporary name and renamed. Only compiles with no ID3DXInclude are cached (the game passes none). `HPRECOMP_SHADER_CACHE=0` turns it off. Failed compiles are not cached.

Timing, 60 distinct sources (deferredFilterLighting.hlsl, DeferredLightPS ps_3_0, flags 0x200), through the thunk:

| Run | Median | p95 |
|---|---|---|
| miss (compile + store) | 7,238 us | 18,781 us |
| hit | 77-86 us | 187-212 us |

## Compile dump

`HPRECOMP_SHADER_DUMP=<dir>` makes `thunk_D3DXCompileShader` write `<key>.hlsl`, `<key>.json` (entry, profile, flags, macros, source length, hr, messages, byte code length) and `<key>.bin` on success. `<key>` is the disk cache key. Unset: no cost. Test: `test_dump` in `tests/test_d3dx_shader.c`.

## NaN class (confirmed: vkd3d gap, triggered by our IEEE option)

Minimal repro (`ps_3_0`, entry `main`):

```hlsl
uniform float4 albedo;
float4 main() : COLOR
{
    float4 c = albedo;
    c.rgb *= 0.0;
    float m = max(max(c.r, c.g), c.b);
    if (m > 1.0)
        c.rgb /= m;
    return c;
}
```

- `VKD3D_SHADER_COMPILE_OPTION_IEEE_754_FP=0` (we pass it when `D3DXSHADER_IEEE_STRICTNESS` is clear): E5021 at the division. Default (1): compiles.
- With IEEE off, vkd3d folds `0 * x` to 0 (`hlsl_constant_ops.c:1716`). The branch condition becomes `0 > 1` and the division becomes `0 / 0`. `fold_div` raises E5021 at fold time for Shader Model < 4 (`hlsl_constant_ops.c:981-986`). The branch is dead, so the NaN never reaches the output. Same pattern in `fold_log2`, `fold_rcp`, `fold_rsq`, `fold_sqrt` (lines 472, 576, 667, 759).
- Game case: `HDRToLDR` with `HDR_PRESERVESATURATION` (`colour.rgb /= maxComponent` under `if (maxComponent > 1.0)`), when `diffuseLight` and `specularLight` fold to 0.
- fxc result NOT run (no Wine). By HLSL semantics the division never executes; fxc's X4579 is about non-finite literals in emitted code. Upstream issue search not done.
- Proposed fix (vkd3d, not applied): do not error in the fold functions; error only when a non-finite constant survives to SM1 constant register allocation. Keeping IEEE on is not equivalent to D3DX, which simplifies `0 * x` without strictness.

## DXVK acceptance

`tools/shaders/dxvk_accept.cpp` (C++: DXVK Native's `d3d9.h` has no C method form). One SDL2 window, KosmicKrisp. Sweep `run/bin/` (555 files: 304 vs, 251 ps): CreateVertexShader/CreatePixelShader pass 555, fail 0. Then `IDirect3DDevice9::Release` aborts: `Assertion failed: (!foundExisting), function dclIoVar, file sm3_io_map.cpp, line 269` (dxbc-spirv SM3 front end, two I/O declarations with the same semantic or register). DXVK translates the byte code later than Create*Shader, so the 555 passes do not prove translation. Cause: see "DXVK sm3 assert".

## DXVK sm3 assert (confirmed: stand-in only; real shaders pass)

- Trigger: every sweep vertex shader (304 of 304); no pixel shader (all 251 `*_ps.bin` translate and tear down cleanly). `1_vs.bin` alone aborts at `sm3_io_map.cpp:269`.
- Stand-in cause: `build_perm.py` gives every varying `TEXCOORDn` in order (`declarations`, `tc += 1`), so `VertexOutput` has `TEXCOORD0`..`TEXCOORD49` on `o1`..`o50`. vs_3_0 allows 12 output registers and usage index 0-15.
- vkd3d gap: no error. `d3dbc.c:3342` writes `usage_idx << 16` without a mask or range check; `validate_max_output_size` runs for geometry shaders only (`hlsl_codegen.c:17010`). The format and vkd3d's own reader use 4 bits (`d3dbc.c:37`, `:1217`).
- DXVK reads 4 bits (`sm3_parser.h:434`), so `TEXCOORD16` on `o17` reads as `TEXCOORD0`. First duplicate pair: `dcl_texcoord0 o1.xyz` and `dcl_texcoord0 o17.xy` (token `0x80100005`). `dclIoVar` treats a repeated semantic on an output as an error (`sm3_io_map.cpp:251-269`). DXVK is right to reject the byte code; an assert instead of a failed translation is a robustness gap only.
- Repro: `tools/shaders/repro/sm3_texcoord16_vs.hlsl` (2 outputs, `TEXCOORD0` and `TEXCOORD16`) and its `.bin`. `dxvk_accept` on the `.bin` aborts with the same assert.
- DXVK has no legacy SM3 path: `src/dxso` is gone at 04e66af0 and `d3d9_options` has no front-end switch.
- Upstream: `gh search issues` for `foundExisting`, `dclIoVar`, `sm3_io_map` in doitsujin/dxvk and doitsujin/dxbc-spirv: 0 results. fxc behaviour NOT run (no Wine).
- Real game shaders do not trigger it: see "Real shaders".
- Next: the stand-in must pack varyings into at most 12 registers with index <= 15, as the real builder does (stand-in now retired). The vkd3d gap (no error for an SM1-3 semantic index > 15 or > 12 vs_3_0 outputs) is worth an upstream report; the game never hits it.

## Real shaders (game capture, notes/shader_oracle.md)

Inputs: `<work>/hp-shader-oracle/out/dump`, 386 compile calls, 286 unique (source, entry, profile, flags). Bytecode `.bin` is Microsoft `d3dx9_41`.

| Check | Result |
|---|---|
| `dxvk_accept` on the 386 Microsoft `.bin` | pass 386 (193 vs, 193 ps), no assert, exit 0 |
| Our layer: `tools/shaders/sweep.c` built against the current `runtime/src/d3dx_shader.c`, `HPRECOMP_SHADER_CACHE=0` | 286 of 286 compile, 0 failures (so no failure classes) |
| `dxvk_accept` on our 286 `.bin` | pass 286 (143 vs, 143 ps), no assert, exit 0 |
| `dcl` lines, ours against Microsoft, all 286 | same semantic, index and register on all 286. 136 differ in a write mask only |
| Highest output register in ours | `o9` (vs_3_0 limit `o11`) |

`dcl` diff for 5 sources: 000000, 000002, 000100 (vs) are identical, masks included (3, 3 and 5 `o` outputs). 000001, 000003 (ps) differ only in `dcl_color0 v0`: Microsoft `.xyz`, ours `.xyzw`. DXVK accepts both.

Scratch evidence (job dir, not durable): `dxvk-sm3/jobs_real.txt`, `real_sweep.log`, `ours/*.bin`, `ms_accept.log`, `ours_accept.log`, `dcl_scan.py`.

## Local vkd3d patch (2026-10-24)

- Source: commit `fed0c3418f815b0bc07533f6fc6f4aa06e8712c3` of
  `https://codeberg.org/vkd3d/vkd3d.git` (build steps: notes/vkd3d_test.md).
- Patch: `tools/vkd3d/patches/0001-vkd3d-shader-hlsl-Emit-one-dot-per-component-in-mul.patch`
  (`git apply` in the source tree). Why: notes/render_artifacts.md.
- Build in the configured tree: `PATH=/opt/homebrew/opt/bison/bin:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin make -j8 libvkd3d-shader.la CC=/usr/bin/clang OBJC=/usr/bin/clang`
  (system bison 2.3 fails on hlsl.y).
- Install prefix the runtime links: `<work>/vkd3d-install`.
  Copy `.libs/libvkd3d-shader.1.dylib` and `.libs/libvkd3d-shader.a` to its `lib/`, then
  `install_name_tool -id <prefix>/lib/libvkd3d-shader.1.dylib` and `codesign -f -s -`.
- Check: `test_mul_dot` in tests/test_d3dx_shader.c (4 dp4, 0 add) fails with the unpatched library.
