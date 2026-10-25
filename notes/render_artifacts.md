# Render artifacts: hatched black streaks and black blocks (2026-10-24)

Result: FIXED. Our vertex shader byte code caused both defects. All 143 of
our vertex shaders computed the same values as Microsoft's in exact
arithmetic, but in a different float form: scalar mul/add chains, never dp4.
vkd3d patch tools/vkd3d/patches/0001 makes mul() emit one dot per output
component, as fxc does; the defects are gone.

Working theory, not measured: multipass draws (depth EQUAL or shadow
compare) recompute the position in different shaders; a MUL+ADD chain lets
the driver fuse to FMA differently per shader, dp4 gives one consistent dot.
DXVK SPIR-V NoContraction: not checked.

## Setup
- Private build (lhp_render) and private HOME, so the save folder, the game
  CachedShaders and our cache are fresh per run (tmp/render/shoot.sh).
- `HPRECOMP_SHADER_OVERRIDE=<dir>` (runtime/src/d3dx_shader.c, d3dx_shader_override):
  D3DXCompileShader returns `<dir>/<sha256 of source>.bin` instead of
  compiling. `tools/shaders/override_dir.py` builds the folder from the oracle
  dump (all, or `--profile vs_3_0|ps_3_0`).
- The game CachedShaders must be empty, or the game skips D3DXCompileShader
  (cache names differ between the Wine oracle and us, so the oracle cache
  cannot be dropped in).

## Hypothesis ledger
| # | Hypothesis | Test | Result |
|---|---|---|---|
| H1 | Our compiler output differs from MS | All 286 shaders MS (ms_all_20s.png) vs ours (ours_all_20s.png), same binary and driver | CONFIRMED: MS clean; ours has streaks and black blocks |
| H1a | Pixel shaders | MS PS + our VS (ms_ps_20s.png) | Defect stays |
| H1b | Vertex shaders | MS VS + our PS (ms_vs_20s.png) | Defect gone |
| H1c | Semantic bug in a VS | float64 evaluator of both disassemblies, inputs by semantic, constants by CTAB name, 8 random trials (tool/vseval.py) | REJECTED: 0 of 143 differ by more than 1e-6. A planted 1e-3 add error is caught in 142 of 143. |
| H1d | Float form differs | Opcode census | CONFIRMED: ours use dp4 in 0 of 143 VS; MS in 139 of 143. Example: MS `mad r0, v0.xyzx, c4.xxxy, c4.yyyx; dp4 r1.x, r0, c39 ...`; ours `mul r0.xyz, v0.xyzx, c39.xyzx; add; add; add c39.w ...` |
| boot H | D24->D32F depth bias scaling | Same DXVK, driver and depth formats in the H1 runs | REJECTED as the cause: MS VS alone removes the defect |
| H2-H4 | Render-state marshalling, depth formats, RT clears | not run | not needed: H1b changes only VS byte code |

## Cause in the compiler
vkd3d-shader `libs/vkd3d-shader/hlsl.y` intrinsic_mul (lines 4487-4515 in
tmp/vkd3d/src) builds `mul(v, M)` as one HLSL_OP2_MUL per term and an
HLSL_OP2_ADD chain. It never makes HLSL_OP2_DOT, so no dp4 reaches d3dbc.
fxc (column_major, vector * matrix) emits one dp4 per output component.

## Fix and proof
- Patch: vkd3d `libs/vkd3d-shader/hlsl.y` intrinsic_mul, inner dimension 2-4
  -> HLSL_OP2_DOT (tools/vkd3d/patches/0001-...). Build: notes/shader_builder.md.
- Census (oracle sources, patched library): 184 of 191 vs_3_0 compile calls
  contain dp4, 3,982 dp4 in total (before: 0 of 143 unique). 2 calls failed
  only in the census tool (smaller option set); the runtime compiles all 286.
- Frames (HPRECOMP_DUMP_FRAMES=2000,5000,8000, menu_skip.input, empty game
  cache, same binary except the vkd3d dylib): Present 5000 is the same
  Privet Drive shot in both runs: before has black road rectangles and
  hatching, after is clean. Present 2000 castle backdrop: towers blotched
  before, clean after. Present 8000 lands in different cutscene shots (the
  cutscene runs on wall-clock time). After 5000 shows a flat pink cat
  (McGonagall's transform); not checked whether that is the game's effect.
- Cache: D3DX_COMPILER_REVISION in the disk-cache key; d3dx_game_cache_check
  (runtime/src/d3dx_shader.c, called from hp_boot_run) zeroes the
  VERSION.DAT version int when the RECOMP_D3DX.TXT stamp is stale, so the
  game's own check (0x9f6260..0x9f63e0: int 0x32, then 16 bytes == table
  0xECFFB4, else delete *.DAT and rewrite VERSION.DAT) wipes the cache.
  Unit test: test_game_cache_check. Not run in a game: a start from a filled
  stale cache.

## Open (historical)
- Which pass disagrees. Next test: bisect MS VS by subset (override_dir.py
  `--only`), or trace D3DRS_ZFUNC/shadow passes.
- Fix design (decision for the lead): emit HLSL_OP2_DOT in intrinsic_mul
  when the inner dimension is 2-4 (vkd3d change, rebuild vkd3d-install), so
  vector*column_major matrix gives dp4 like fxc. Check: vseval opcode census
  shows dp4 in our VS, and before/after frames show the defect gone.

## Flat standing cat after the first patch (2026-10-24)

- Cause: the first patch version built every mul() row and column from
  per-element loads (hlsl_add_load_component, hlsl.y:936, copies the whole
  operand each call). For a relative-indexed float4x4 (computeVertexSkin4,
  skinMatrix[int(i.x)]) all 16 loads per bone were hoisted before the dots:
  2 skinned vs_3_0 cutscene shaders failed with E9015 (r32-r37 over the 32
  temps), logged as "D3DXCompileShader(vs_3_0, vertexProgram) failed".
  The pre-fix run had 0 failures.
- Effect: in both first-patch runs a flat standing cat (magenta once, green
  once) is drawn beside the sitting cat in the Dumbledore shot; the pre-fix
  run's same shot (before2 frame_5000) has no standing cat.
- Fix: the patch now emits a dot only when both operands are one register:
  the vector operand itself, and a column of a column-major matrix by path
  index (one c72[a0.x] load, as fxc). Other shapes keep the MUL+ADD chain.
- Proof: 286 of 286 oracle shaders compile. vseval: 0 of 143 VS differ
  from MS. dp4 in 139 of 143 VS, 1,950 total, the same as MS. Skin shader:
  505 lines, max r24 (unpatched: 707 lines, r24). Game run after3: 376
  compiles, 0 failed. Test test_mul_dot_skin (16 dp4).
- Not captured: the same Dumbledore shot in a fixed run (timing moves it;
  after3 4800-5200 and after4 4000-6000 step 100 missed it). Those 30
  frames show no flat cat.
