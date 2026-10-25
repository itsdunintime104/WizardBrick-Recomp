# vkd3d-shader test for D3DXCompileShader / D3DXGetShaderConstantTable (measured)

Date: 2026-10-24. Machine: arm64 macOS (Darwin 25.6.0). Nothing from the exe was executed; it was parsed as data.
Scratch (build, probe programs, logs): `<job tmp>/vkd3d/` (`src`, `build`, `install`, `winesrc`, `winebuild`, `test/vkt.c`, `test/ctab.py`, `test/mkprelude.py`, `test/iter.sh`, `test/small.sh`, `extract/`). Nothing is stored in the repo except this note and `tools/pe/extract_shaders.py`.

## Verdict

| Item | Result |
|---|---|
| A. Build native arm64 library | PASS |
| B. Compile the game HLSL to SM1-3 byte code | PARTIAL (compiler works; uber shader needs the engine's builder text first) |
| C. Read back shipped blobs, recover CTAB | PASS for disassembly (451/451); CTAB is in the bytes but no vkd3d API returns it |
| D. Chain coverage | `D3DXCompileShader`: mostly. `D3DXGetShaderConstantTable`: not at all |

**D3DXCompileShader is mostly covered by `vkd3d_shader_compile`. `D3DXGetShaderConstantTable` is not covered: vkd3d-shader 2.1 has no function that reads a constant table from legacy byte code. The first thing to write by hand is a CTAB parser (about 60 lines of C) that fills an `ID3DXConstantTable`; the same parser also serves the `D3DXCompileShader` out-parameter.**

## A. Library obtained and built

No package exists: `brew search vkd3d` and `brew info vkd3d` return "No available formula with the name vkd3d". Built from source.

Version: `vkd3d-shader 2.1 (git fed0c34)`, commit `fed0c3418f815b0bc07533f6fc6f4aa06e8712c3` (2026-10-08, `vkd3d-shader/msl: Implement VSIR_OP_ISNAN.`) of `https://codeberg.org/vkd3d/vkd3d.git` main, `git clone --depth 1`.

Output: `install/lib/libvkd3d-shader.1.dylib`, `Mach-O 64-bit dynamically linked shared library arm64`, 1,660,040 bytes, depends only on `/usr/lib/libSystem.B.dylib`.

Steps (each failure and fix on this machine):

1. Tools: `brew install libtool vulkan-headers vulkan-loader` (bison 3.8 and flex came from brew; macOS bison is 2.3). `autogen.sh` needs `libtoolize`: use `PATH=/opt/homebrew/opt/libtool/libexec/gnubin:...`.
2. Generated headers need `widl`. configure only warns `widl is required to build header files`; the build then fails with `hlsl.h:25:10: fatal error: 'vkd3d_d3dx9shader.h' file not found`. There is no widl package. Built it from Wine: `git clone --depth 1 https://github.com/wine-mirror/wine.git` (Wine 11.20, `840a939e`), then `configure --enable-tools --enable-archs=x86_64 ...` and `make tools/widl/widl` (`file`: arm64). The default `--enable-archs` fails with `configure: error: aarch64 PE compiler not found`.
3. Perl module JSON is required: `configure: error: perl module 'JSON' not found`. Installed into the scratch dir with `cpan` and `PERL_MM_OPT=INSTALL_BASE=...`, run with `PERL5LIB`.
4. This shell's `PATH` has x86_64 tools first (`/opt/local/bin/otool`, `/usr/local/bin/gcc`): configure fails with `xcrun: error: unable to load libxcrun ... need 'x86_64'`. Fix: `PATH=/opt/homebrew/...:/usr/bin:/bin:...`, `CC=/usr/bin/clang OBJC=/usr/bin/clang OTOOL=/usr/bin/otool`.
5. `--without-metal` breaks configure (`conditional "am__fastdepOBJC" was never defined`). Leave Metal auto-detected.
6. Configure:
   `CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib WIDL=<widl> ../src/configure --prefix=<scratch>/install --disable-demos --disable-tests --without-ncurses --without-opengl --without-xcb --without-spirv-tools`
7. Generated headers are not built by `make libvkd3d-shader.la`. Built by hand: `make include/vkd3d_*.h` (the `widl_headers` list), `make include/private/spirv_grammar.h`, `make include/private/vkd3d_version.h`. Then `make -j8 libvkd3d-shader.la` returned rc=0, and `make install-libLTLIBRARIES` installed it.

Header read for names: `<job tmp>/vkd3d/src/include/vkd3d_shader.h` (the one in the source tree, matching the built library).
Used: `vkd3d_shader_compile(const struct vkd3d_shader_compile_info *, struct vkd3d_shader_code *out, char **messages)`, `VKD3D_SHADER_SOURCE_HLSL`, `VKD3D_SHADER_SOURCE_D3D_BYTECODE`, `VKD3D_SHADER_TARGET_D3D_BYTECODE`, `VKD3D_SHADER_TARGET_D3D_ASM`, chained `struct vkd3d_shader_preprocess_info` (macros, `pfn_open_include`, `pfn_close_include`) and `struct vkd3d_shader_hlsl_source_info` (`entry_point`, `profile`), option `VKD3D_SHADER_COMPILE_OPTION_API_VERSION`.
The header's table at lines 3134-3151 lists `HLSL -> D3D_BYTECODE`, `HLSL -> D3D_ASM`, `D3D_BYTECODE -> D3D_ASM` as supported pairs.

Probe program: `test/vkt.c` (C, linked against the built dylib). Modes: `hlsl` (HLSL to byte code), `asm` (byte code to d3d-asm text).

## Extraction (tools/pe/extract_shaders.py)

```
python3 -I tools/pe/extract_shaders.py orig.nosync/lhp_win32.exe <out>
hlsl deferredFilterCommon     file_offset=0xa8aa28 bytes=12015
hlsl deferredFilterShadows    file_offset=0xa8d918 bytes=45022
hlsl deferredFilterLighting   file_offset=0xa988f8 bytes=9581
hlsl fog                      file_offset=0xa9ae68 bytes=1577
hlsl hdr                      file_offset=0xa9b498 bytes=2227
hlsl ubershader               file_offset=0xa9bed0 bytes=213219
wrote 6 HLSL bodies (283641 bytes) and 451 shader blobs (567896 bytes)
blobs by profile: ps_1_1 x10, ps_2_0 x31, ps_3_0 x337, vs_1_1 x28, vs_2_0 x13, vs_3_0 x32
```
Offsets match `notes/shader_pipeline.md` without being hardcoded. HLSL bodies are found as long printable runs in `.data` with HLSL keywords; blobs by version token, then a comment token with `CTAB`, then a walk to `0x0000FFFF`. Totals match the measured facts (283,641 and 451).

## B. Compiling the game HLSL

Entry points: `vertexProgram`, `fragmentProgram` (uber), `DeferredLightPS` (lighting), `DeferredFilterQuadVS`, `DeferredFilter3DVS` (common), `DebugVisualizeShadowmapVS` (shadows).

### B1. Uber shader, bare (no defines): FAIL as expected

```
vkt hlsl ubershader.hlsl fragmentProgram ps_3_0 out.bin extract/hlsl
include not found: .../fog.hlsl          (first run, include dir bug in probe; fixed)
```
After the include directory was fixed, the first errors are, verbatim:
```
input:181:17: E5005: Identifier "exposure" is not declared.
input:181:6: E5002: Can't implicitly convert from <error type> to half.
input:338:17: E5005: Identifier "layer0_diffuse" is not declared.
input:395:111: E5005: Identifier "fs_layer0_color" is not declared.
input:403:22: E5005: Identifier "fs_vertexOpacity" is not declared.
input:1065:8: E5005: Identifier "COLOR_FACTOR" is not declared.
input:1562:66: E5005: Identifier "vin" is not declared.
vkd3d_shader_compile rc=-4
```
Plus 142 stderr lines `fixme:vkd3d:hlsl_yylex Unsupported preprocessor #pragma directive` for the `nu2_*` pragmas (harmless; the pragma is ignored).
Cause: the uber source is a template. `VertexInput`, `VertexOutput`, `FragmentOutput`, `vin`, `vout`, the `varying_*`, `vs_*` and `fs_*` locals and every uniform come from the engine's HLSLShaderBuilder, which expands `#pragma nu2_declare/nu2_use/nu2_anchorPoint`. The `FragmentOutput` struct sits under `#if defined(_SHADERBUILDER_DEBUG)` and does not help (`-D_SHADERBUILDER_DEBUG` leaves the same error classes).

### B2. Uber shader with a minimal define set: no language error; needs the builder's declarations

Defines: `-DHLSL_LANGUAGE -DPIXEL_SHADER_VERSION=30 -DVERTEX_SHADER_VERSION=30 -DRENDER_TARGET_COUNT=1 -DLIGHTING_MODEL=1` plus `-DBUILD_PIXEL_SHADER` or `-DBUILD_VERTEX_SHADER`. The raw source with these defines still fails (36 `E5005 not declared` for the ps).

To test the language, I wrote a throwaway stand-in for the builder (`test/mkprelude.py`, scratch only): it turns the 140 `nu2_declare` lines into plain structs and globals, and adds `uniform float4 <name>;` for each name vkd3d reports as undeclared (7 or fewer names: `ambientColor COLOR_FACTOR exposure layer0_diffuse sceneAmbientColor viewProj world`; `world` and `viewProj` as `float4x4`). Result:

```
iter.sh fragmentProgram ps_3_0 BUILD_PIXEL_SHADER  -> vkd3d_shader_compile rc=0, output bytes=428
iter.sh vertexProgram   vs_3_0 BUILD_VERTEX_SHADER -> vkd3d_shader_compile rc=0, output bytes=1348
vertexProgram vs_2_0 -> rc=0 (736 bytes);  vs_1_1 -> rc=0 (736 bytes);  fragmentProgram ps_2_0 -> rc=0 (428 bytes)
richer ps_3_0 (-DLIGHTING_MODEL=5 -DSURFACE_TYPE=1 -DLAYER0_DIFFUSEENABLE -DFOG_STAGE=2) -> rc=0 (544 bytes)
```
The output disassembles (`vkt asm`) as a valid shader. These are tiny because the stand-in leaves most inputs unconnected; they prove the front end parses and lowers the uber template's language for these paths, not that all about 140 permutations compile.
Failures found:
- `fragmentProgram ps_1_1`: `E9015: Register r2 exceeds limits for shader type 0, version 1.1.` (4 sites). Not determined whether this is a vkd3d limit or my define set (the uber has dedicated `PIXEL_SHADER_VERSION == 11` paths I did not enable).
- `E5002: The half type is not allowed for global variables.` (64 sites) when the stand-in declared `half` globals. vkd3d rejects `half` at global scope; D3DX accepts it. The builder must emit `float` for globals (done in the stand-in). `half` locals and parameters compile.
- The Kajiya-Kay/aniso block redefined `Hn` (`E5006`) when `LIGHTING_MODEL` was left undefined. That is a define-set artefact, gone once `LIGHTING_MODEL=1`.
- D3DX flags 0x200 (`D3DXSHADER_AVOID_FLOW_CONTROL`) and 0x10000 that the game passes were not mapped or tested.

### B3. Smaller bodies: PASS

`VertexShaderSemantics.fx` is not embedded in the exe, so the include was stubbed with an empty file that gained `uniform float4 <name>;` lines for the names vkd3d reported (5 names: `viewProj vs_inverse_projection_xy vs_projection_params vs_viewportScaleBias world`).

| Source | Entry, profile | Result |
|---|---|---|
| deferredFilterCommon.hlsl | DeferredFilterQuadVS, vs_3_0 | rc=0, 444 bytes |
| deferredFilterCommon.hlsl | DeferredFilter3DVS, vs_3_0 | rc=0, 656 bytes |
| deferredFilterShadows.hlsl | DebugVisualizeShadowmapVS, vs_3_0 | rc=0, 1024 bytes (W5300 truncation warnings only) |
| deferredFilterLighting.hlsl (includes Shadows, Common) | DeferredLightPS, ps_3_0 | rc=0 (W5300 warnings only) |
| hdr.hlsl via a 4-line wrapper `main` | main, ps_3_0 and ps_2_0 | rc=0, 188 bytes each |

Cross-check against the exe's own blob `ps_3_0_89fc98.bin` (the Microsoft compile of the same lighting file): vkd3d's output CTAB lists `g_AlbedoTex s6`, `g_Light c180 x3`, `g_SceneDepthTex s0`, the same registers as the Microsoft output (vkd3d also lists `g_LightCommon c187`, `g_NormalsTex s2`, `g_StencilValuesTex s1` because the exe's blob was built from a specialised define set). Register allocation for these matches.
fog.hlsl was not compiled separately (it only holds helper functions that read `fog_params` and `worldCamPos`).

## C. Reading the shipped blobs

```
vkt asm ps_1_1_88cc58.bin
vkd3d_shader_get_version: vkd3d-shader 2.1 (git fed0c34)
vkd3d_shader_compile rc=0 (VKD3D_OK=0)
ps_1_1
tex t0
tex t1
mul_x2 r0.xyz, t1, t0
+ mov r0.w, t0.w
```
Source type `VKD3D_SHADER_SOURCE_D3D_BYTECODE`, target `VKD3D_SHADER_TARGET_D3D_ASM`. Also checked: `ps_3_0_88d290.bin` (4012 bytes, `def`/`dcl_*`/`texkill`/`texldl` output), `ps_3_0_89fc98.bin`, `vs_3_0_88cd50.bin` (`dcl_position o0`, `mad o1.xy, v1, c57.zwzw, c57`), `vs_1_1_88c678.bin`.
All 451 extracted blobs: `disasm ok=451 fail=0`.

CTAB: the disassembly does not print the constant table (no comment lines for `g_Light` and so on), and no `vkd3d_shader_*` function in the header reads it. `grep -i ctab include/vkd3d_shader.h` finds nothing; in `libs/vkd3d-shader/d3dbc.c` CTAB appears only in `d3dbc_compile` (line 3423, writing `TAG_CTAB`) and the reader `shader_sm1_read_comment` (line 1246) only logs comments. The CTAB is intact in the bytes. My parser (`test/ctab.py`, about 15 lines of Python, standard `D3DXSHADER_CONSTANTTABLE` layout, offsets relative to the byte after `CTAB`) recovers all 451 tables:
```
ps_3_0_89fc98.bin creator='Microsoft (R) HLSL Shader Compiler 9.26.952.2844' target='ps_3_0' n=3
   g_AlbedoTex      set=3 reg=c6 count=1
   g_Light          set=2 reg=c180 count=3
   g_SceneDepthTex  set=3 reg=c0 count=1
vs_3_0_88cd50.bin: vs_inverse_projection_xy c54, vs_projection_params c10, vs_viewportScaleBias c57
```
These agree with the disassembly (`c180-c182` used by `g_Light`; `c10` as `vs_projection_params`).
Byte code that vkd3d itself emits also carries a CTAB (creator `vkd3d-shader 2.1 (git fed0c34)`), so the same parser works on `D3DXCompileShader` output. The `Target` string offset in vkd3d's CTAB did not decode to a profile string in my parser (it read `\x1c`); not checked further.

## D. Coverage

| D3DX call | vkd3d-shader | Gap |
|---|---|---|
| `D3DXCompileShader` source, entry, profile | `vkd3d_shader_compile` with `hlsl_source_info` | none |
| `D3DXCompileShader` D3DXMACRO defines, ID3DXInclude | `vkd3d_shader_macro`, `pfn_open_include` / `pfn_close_include` | thin adapter |
| `D3DXCompileShader` error buffer | `messages` out parameter | thin adapter |
| `D3DXCompileShader` flags (0x200, 0x10200) | only `COMPILE_OPTION_*` (pack order, strip debug) | flags unmapped, effect untested |
| `D3DXCompileShader` constant-table out-parameter | none | hand-written |
| `D3DXGetShaderConstantTable` | none | hand-written |
| Byte code in, text out (debug) | `D3D_BYTECODE -> D3D_ASM` | none |

Open risks: the uber shader still depends on a re-implemented HLSLShaderBuilder (the pragma expansion) before any permutation compiles; ps_1_1 fails on the uber with a register-limit error; `half` globals are rejected; output equivalence with the Microsoft compiler was checked only by register layout for one shader, not by running it.

**First thing to write by hand: the CTAB reader that implements `ID3DXConstantTable` (names, register set/index/count, and the type info for `D3DXCONSTANT_DESC`), used by both `D3DXGetShaderConstantTable` on cached blobs and the constant-table out-parameter of `D3DXCompileShader`. Second: the builder's `nu2_*` pragma expansion.**
