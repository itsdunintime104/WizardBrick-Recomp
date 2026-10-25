# Shader pipeline of lhp_win32.exe (measured)

Scope: static measurement of `orig.nosync/lhp_win32.exe` (14,598,144 bytes) and the seven `GAME*.DAT` archives.
Nothing was executed. VA = 0x400000 + file offset for .text, .rdata and .data (raw offset equals RVA in these sections).
Scripts: `<job tmp>/` (scan2.py, frags.py, imp.py, scan_dat.py, dis.sh).

## Verdict

**Runtime HLSL compiler REQUIRED** (for full fidelity) unless the shipped shader cache is recovered.
Reason: the engine builds HLSL source at run time from one 213 KB uber-shader plus about 140 permutation macros, then calls `D3DXCompileShader`. Only 451 fixed shaders are precompiled in the exe.
The offline route exists in principle (the engine itself has a shader-cache format and a `-DumpShaders` mode) but is not proven (see Q4).

## Q1. HLSL source in the exe: YES, 6 embedded text bodies, 283,641 bytes, all in `.data`

| File offset | Bytes | Identity (from the text itself) |
|---|---|---|
| 0xa8aa28 | 12,015 | `deferredFilterCommon.hlsl` ("File : ... Author ... Format : HLSL/Cg") |
| 0xa8d918 | 45,022 | `deferredFilterShadows.hlsl` (`#include "DeferredFilterCommon.hlsl"`) |
| 0xa988f8 | 9,581 | `deferredFilterLighting.hlsl` (`#include "DeferredFilterShadows.hlsl"`) |
| 0xa9ae68 | 1,577 | `fog.hlsl` ("Used by Ubershader and debris.hlsl") |
| 0xa9b498 | 2,227 | `hdr.hlsl` body (starts `half4 HDRToLDR(...)`) |
| 0xa9bed0 | 213,219 | the uber shader: starts `#define UBERSHADER`, 6,229 lines |

Method: printable runs of at least 150 chars inside `.data` that contain HLSL keywords (frags.py). Output of the bodies: `hlsl_all.txt` in the tmp dir.
Referenced file names (strings at 0x91cd94..0x91ce00): `VertexShaderSemantics.fx`, `hdr.hlsl`, `fog.hlsl`, `deferredFilterLighting.hlsl`, `deferredFilterShadows.hlsl`, `deferredFilterCommon.hlsl`.
The body of `VertexShaderSemantics.fx` is NOT embedded. The uber source says "Engine parameters - mapped on to declarations in VertexShaderSemantics.fx" and then `#include "VertexShaderSemantics.fx"`. The engine instead has 112 `SEMANTIC_*` strings (first at 0x86c50c) and emits `#define g_X vs_x` style lines itself.
Uber-shader facts:
- 724 `#pragma nu2_use(...)` and 140 `#pragma nu2_declare(...)` lines, 5 `#pragma nu2_anchorPoint(attribute|varying|vs_local|fs_local|uniform)`. These are the engine's own pragmas. The builder replaces them with generated declarations.
- 671 `#if/#ifdef/#ifndef/#elif` lines, 183 distinct macro names in conditionals.
- `#if defined(BUILD_EFFECT)` block at about file offset 0xab0xxx (string "technique" at file offset 11336679) with `technique TECHNIQUE_BUILTSHADER { pass p0 { VertexShader = compile vs_3_0 vertexProgram(); ...`. Entry points are `vertexProgram` and `fragmentProgram`.
- Search path string at 0x91cf38: `./shaders;../nu2api/shaderBuilder;C:\projects\nu2api\shaderBuilder`. These are development paths; no such files ship in the Steam folder.
- "// Dummy comment to get rid of old cached shaders on PC / And again" is the last text in the uber source (file offset 11337609). Editing the source text changes the cache key (see Q4).

## Q2. Compiled D3D9 byte code

### In the exe: YES, 451 shader blobs, 567,896 bytes, 298 distinct

Scanner (scan2.py): version token `0xFFFE03xx/0xFFFF03xx` etc. at a 4-byte aligned offset, followed directly by a comment token and `CTAB`, then walk to the `0x0000FFFF` end token.

| Profile | Count |
|---|---|
| vs_1_1 | 28 |
| vs_2_0 | 13 |
| vs_3_0 | 32 |
| ps_1_1 | 10 |
| ps_2_0 | 31 |
| ps_3_0 | 337 |
| total | 451 |

- Location: `.rdata`, file offsets 0x88c678 .. 0x917390 (contiguous, 0 to 52 bytes between blobs). Blob sizes 140 to 4,012 bytes.
- Cross-check with strings: `Microsoft (R) HLSL Shader Compiler 9.26.952.2844` x441 and `Microsoft (R) D3DX9 Shader Compiler 9.15.779.0000` x10 = 451. Matches the blob count exactly.
- Example: first blob at 0x88c678 (VA 0xc8c678), `vs_1_1`, 756 bytes, bytes `01 01 fe ff fe ff 24 00 43 54 41 42 ...`.
- Constant names in the CTAB tables are deferred-lighting and shadow names: `g_SceneDepthTex` (235 blobs), `g_AlbedoTex`, `g_Light`, `g_LightCommon`, `g_ShadowCommon`, `g_ShadowMapTex`, `g_SpotViewProj`, `g_LuminanceTex`. So these are the precompiled deferred light/shadow/post shaders.
- Consumption: every one of the 451 blobs is referenced by a `push imm32` in `.text` (819 reference sites, range VA 0x9a3307..0xa28424). Example at VA 0x9a3307:
  `mov eax,[0x10b0ed4]` (device), `mov ecx,[eax]`, `mov edx,[ecx+0x16c]`, `push &out`, `push blob(0xc8f4f8)`, `push eax`, `call edx`.
  Vtable offset 0x16c / 4 = 91 = `IDirect3DDevice9::CreateVertexShader`; offset 0x1a8 / 4 = 106 = `CreatePixelShader`. So these go straight to the device with no D3DX call.
- There is no pointer table in data to them (0 data references). They are registered by straight-line code.

### In the DAT files: NO raw byte code found; most payload is opaque

Streaming scan of all seven archives, 6.2 GB total, 59 s (scan_dat.py):
- `Microsoft (R) HLSL` string: 0 hits. Version token + comment + `CTAB` pattern: 0 hits.
- `CTAB`: 5 hits in GAME.DAT, 1 in GAME3.DAT (about 4x the chance rate for a 4-byte string over 6.2 GB; contexts checked: GAME.DAT 0x317a4a28 sits inside random-looking bytes with no version token before it).
- `DXBC`: 1 hit (GAME3.DAT 0x8a8d1c0), random context. `.fx` 3-byte hits (323 across the seven files) are chance in random data (example GAME6 0x37d0bb is inside high-entropy bytes).
- Effect magics `01 09 ff fe` / `fe ff 00 01`: 8 hits, all in random-looking data.
- zlib level-1 ratio on 16 evenly spaced 64 KB samples per file: 0.99 to 1.00 for 14 of 16 samples in every file. So most payload is compressed, encrypted or already-compressed media. I did not try to decode it.
- Format facts seen: header `BEGIN_APP_ID_STRING` `PakDat v1.01` `END_APP_ID_STRING` at offset 0x8. Plain-text members are stored uncompressed: the sound list at 0x0..0x70000 (`SAMPLE: Audio\_SoundFX\...`), scripts around 0x616e0000, and one big text file at 0x1cdf0000..0x1cfb0000. The tail of each file is a table of ascending 32-bit values (hashes or offsets; not decoded).
- I did not parse the PakDat directory. So absence of raw byte code in the DATs does NOT prove there is no cached byte code inside compressed members.

## Q3. Which D3DX entry points consume what

13 `d3dx9_41` imports (imp.py): `D3DXGetShaderConstantTable`, `D3DXCreateTextureFromFileInMemoryEx`, `D3DXCreateCubeTextureFromFileInMemory`, `D3DXCreateEffectFromFileA`, `D3DXCreateFontA`, `D3DXCreateRenderToSurface`, `D3DXCreateTextureFromFileInMemory`, `D3DXSaveSurfaceToFileInMemory`, `D3DXCreateEffect`, `D3DXCompileShader`, `D3DXCreateEffectCompiler`, `D3DXCreateBuffer`, `D3DXMatrixMultiply`. All calls go through `jmp [IAT]` thunks at VA 0xa9608a..0xa960d0.

| Import | Call sites (VA) | Reached? |
|---|---|---|
| `D3DXCompileShader` | 0xa05007, 0xa0506b (both in one shader-object creator at about 0xa04e20) | Yes: the live path. Args at 0xa05007: source ptr, length, defines 0, include 0, entry name `0xd1c22c`, profile string, flags 0x200 (or 0x10200 when profile major < 3), plus out ptrs for shader, errors, constant table. |
| `D3DXGetShaderConstantTable` | 0x9f661c, 0x9f6629 (cache loader, one call each for the VS blob and the PS blob) | Yes. It runs on byte code read back from a cache file. |
| `D3DXCreateEffectFromFileA` | 0x96c4fa, inside a 0x96c4d0 wrapper | Wrapper has 0 direct callers and 0 data references: not reached (dead or unreferenced). |
| `D3DXCreateEffect` | 0x96c5ff, wrapper at 0x96c5d0 | Same: 0 callers, 0 references. |
| `D3DXCreateEffectCompiler` | 0x93bc0a, function at 0x93bbe0 | Same: 0 callers, 0 references. |

Not found: any `.fxo`, `.cso`, `.vsh`, `.psh` string in the exe. `.fx` appears only as `VertexShaderSemantics.fx` (an include name). So the effect (.fx) API imports are not on a live path. The engine uses the `TECHNIQUE_BUILTSHADER` text only as a source wrapper for `D3DXCompileShader`.

Control flow of the live path (shader object creation at VA 0xa04e20, read from disassembly):
1. Call 0x9f6100 (cache load). It builds a path with `sprintf` and the formats `%s\%s\%s\CachedShaders` (0xd1bdd4), `%s\VERSION.DAT` (0xd1be04) and `%s\%d%08x.DAT` (0xd1bdc4), opens the file through the engine file layer (0x966870 / 0x928430), reads two length-prefixed blobs through 0x9641a0, and runs `D3DXGetShaderConstantTable` on each.
2. Hit: `device->CreateVertexShader` (vtable +0x16c) and `CreatePixelShader` (+0x1a8) with the cached blobs. No compiler.
3. Miss: choose profile/entry strings (`vs_1_1 ... vs_3_0`, `ps_1_1 ... ps_3_0`, `ps_2_a`, `ps_2_b`, `vs_2_a`, `fragmentProgram`, `vertexProgram`), then `D3DXCompileShader` on the generated source.

## Q4. Offline route

Evidence that the source is built at run time:
- HLSLShaderBuilder / ShaderBuilderGen / NuShaderManager<NuShaderObjectPC> are RTTI classes (`.?AVHLSLShaderBuilder@@` near file offset 11376156).
- The builder emits preprocessor text: `#define `, `#undef `, `uniform `, `uniform float4 reserve[%d] : c0;`, `// Pixel shader uniforms`, `// Vertex shader uniforms`, `float4(%f, %f, %f, %f)`, semantic names `TEXCOORD0..7`, `COLOR0/1`, `ATTR0..14`, swizzles `.xy .xyz .xyzw`, plus the directive names `define undef ifdef ifndef endif elif else pragma` (its own preprocessor for the `nu2_*` pragmas). The target language switch is `HLSL_Lang` / `CG_Lang1.4`.
- About 140 permutation macro names are named in `.rdata` 0x91a0c0..0x91b8f0, for example `LIGHTING_MODEL`, `LIGHTMAP_STAGE`, `FOG_STAGE`, `ENVMAP_STAGE`, `REFRACTION_STAGE`, `FRESNEL_STAGE`, `SURFACE_TYPE`, `LAYER0..3_DIFFUSEENABLE`, `COMBINE_OP_0..3`, `UVSET_COUNT`, `FUNC_UVSET_0..6`, `MOTION_BLUR_PASS`, `VERTEX_TRANSFORM_SKINNING`, `SHADERTYPE_LEGO/WATER/CUSTOM`, `PIXEL_SHADER_VERSION`, `VERTEX_SHADER_VERSION`, `BUILD_VERTEX_SHADER`, `BUILD_PIXEL_SHADER`. Each material permutation is a different define set over the same uber source.
- So the source is composed per material, not shipped per shader. `D3DXCompileShader` is fed generated text on a cache miss.

Evidence that an offline route exists:
- A cache layer exists and stores compiled byte code (VS blob + PS blob, read back by `D3DXGetShaderConstantTable`). The cache file name is `%d%08x.DAT`: one digit plus an 8-digit hex hash, for example `61141c2d4.DAT`.
- `-DumpShaders` (string at file offset 0x919890; code uses `C:\genShaders`, `\filetable.txt`, "Processed %d gsc files", "Dumping shaders for %s", "Failed to open filetable.txt! Can't produce shader cache dump without it!"). The engine can walk every `.gsc` scene file listed in `filetable.txt` and generate the whole cache. So the permutation set is enumerable from game data.
- The text file table stored in GAME.DAT (0x1cdf0000 region, 25,567 CRLF lines) lists 343 `...\CachedShaders\*.DAT` entries (first at file offset 0x1cde9b6c): `VERSION.DAT` plus 342 hash-named files (336 distinct names, 337 including VERSION.DAT). Paths are developer-machine absolute (`C:\Documents and Settings\ukjnewton\Application Data\Warner Bros. Interactive Entertainment\LEGO(R) Harry Potter(TM)\CachedShaders\...`). This is a lower bound of about 336 to 342 shader pairs used by the retail game on the build machine.

What is NOT proven:
- Whether those 342 cache files are actually stored in the DAT archives. Their names are listed, but the payload is opaque and no raw byte code was found (Q2).
- Whether the cache lookup would match them on a user machine (the listed paths are the developer's AppData path; the code builds the path from runtime strings).
- No `CachedShaders` directory exists in `~/.wine-steam-11/drive_c` (game never run there).

Conclusion for the graphics layer: three sources of shaders, in order of how little work they need.
1. 451 precompiled SM1.1 to SM3.0 blobs in the exe: extractable now (offsets in Q2), no compiler needed on the device. Cross-compile ahead of time.
2. Deferred/post sources (`deferredFilter*.hlsl`, `fog.hlsl`, `hdr.hlsl`): already present as text, but the 451 blobs likely cover them.
3. Uber-shader permutations (about 336 to 342 in the dev cache): need either (a) the shipped or regenerated cache files (needs the PakDat directory parsed, or a run of `-DumpShaders`), or (b) a run-time path: reimplement HLSLShaderBuilder text generation and feed an HLSL compiler (`D3DXCompileShader`, `d3dcompiler`, or DXC) at start-up.

## One measurement that would overturn the verdict

Parse the PakDat directory (the ascending 32-bit table at the tail of each DAT plus the 25,567-line text table), decode one `CachedShaders\*.DAT` member, and check for a `0xFFFE0300` / `0xFFFF0300` token stream with `CTAB`. If those members are in the archive and their names map to the hashes the code computes, an offline cross-compile of all uber permutations is viable and no run-time compiler is needed.
