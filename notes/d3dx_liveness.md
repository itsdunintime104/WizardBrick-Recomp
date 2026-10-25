# d3dx9_41 import liveness (measured)

Date: 2026-10-24. Static analysis of `orig.nosync/lhp_win32.exe` as data (capstone 32-bit, nothing executed).
Method: IAT slots from `recon/analysis.json` (`pe.iat`); the `jmp [slot]` thunk for each; every `call`/`jmp` to the thunk inside the Ghidra function ranges (`recon/ghidra_functions.json`); the containing function; its callers, `push`/`mov` immediates and data dwords that equal its entry. Each function with no reference was cross-checked by a raw scan of the whole file: every `E8`/`E9` rel32 in `.text` and every dword equal to the entry. All entries checked are preceded by `C3 CC..` padding, so they are real function starts.
Scratch scripts (deleted with the job): `<job tmp>/d3dx/py/` (`live.py`, `raw.py`, `tbl.py`, `dis.py`, `disp.py`).

## Verdict

| Import | Thunk | Call sites (containing function) | Reached? | Verdict |
|---|---|---|---|---|
| D3DXGetShaderConstantTable | 0xa960c6 | 0x9f661c, 0x9f6629 (0x9f6100, shader cache load) | 0x9f6100 ← 0xa04e20 ← 0xa06af0 ← 0xa21ad0 (3 callers) ← 0xa288a0 ← 0xa32310 (117 callers) | LIVE |
| D3DXCreateTextureFromFileInMemoryEx | 0xa960ae | 0x96c59d (0x96c550), 0x985238 (0x984f20) | both: 0 callers, 0 immediates, 0 data refs; raw scan: none | DEAD |
| D3DXCreateCubeTextureFromFileInMemory | 0xa960b4 | 0x96c5c5 (0x96c5b0), 0x9f6da9 (0x9f6d70) | both: no reference of any kind | DEAD |
| D3DXCreateEffectFromFileA | 0xa960a2 | 0x96c4fa (0x96c4d0) | no reference | DEAD |
| D3DXCreateFontA | 0xa9609c | 0x96c4be (0x96c480) | no reference | DEAD |
| D3DXCreateRenderToSurface | 0xa96096 | 0x96c474 (0x96c450) | no reference | DEAD |
| D3DXCreateTextureFromFileInMemory | 0xa960a8 | 0x96c525 (0x96c510), 0x96c545 (0x96c530), 0x9b0f55 (0x9b0da0), 0x9f6d34 (0x9f6ce0) | 0x9b0da0 is slot 3 of vtable 0xd194ac (11 slots; RTTI `.?AVNuSSAOFilter@@` via locator 0xde467c); the vtable is stored at 0x94bcda, 0x9c4176, 0x9d7686, 0xa2f36b. The other three functions: no reference | LIVE (one site: 0x9b0f55) |
| D3DXSaveSurfaceToFileInMemory | 0xa960c0 | 0x9bb64e (0x9bb4f0) | 0x9bb4f0 ← 0x9c002a, 0x9c3021 (in 0x9c0000: 3 callers, 48 data refs) | LIVE |
| D3DXCreateEffect | 0xa960ba | 0x96c5ff (0x96c5d0) | no reference | DEAD |
| D3DXCompileShader | 0xa960cc | 0xa05007 (vertex), 0xa0506b (pixel) (0xa04e20, shader object create) | 0xa04e20 ← 0xa06af0 ← ... as for the constant table | LIVE |
| D3DXCreateEffectCompiler | 0xa9608a | 0x93bc0a (0x93bbe0) | no reference | DEAD |
| D3DXCreateBuffer | 0xa96090 | 0x93bbee (0x93bbe0, the same dead function) | no reference | DEAD |
| D3DXMatrixMultiply | 0xa96084 | 78 sites in 47 functions (first 0x9362b9 in 0x936290) | e.g. 0x936490 has 25 callers | LIVE |

Shader imports that are LIVE: `D3DXCompileShader`, `D3DXGetShaderConstantTable` (implemented in `runtime/src/d3dx_shader.c`).
Other LIVE imports, not shader functions: `D3DXCreateTextureFromFileInMemory` (SSAO filter), `D3DXSaveSurfaceToFileInMemory` (screenshot path), `D3DXMatrixMultiply` (math).
Not checked: calls through computed addresses that no dword in the file encodes. None of the DEAD functions has such a reference in the raw scan.

## Methods the game calls on the returned objects

ID3DXBuffer (`ppShader`, `ppErrorMsgs`), in 0xa04e20:
- `+0x08 Release`: every shader, error and table pointer (0xa0501a .. 0xa05249).
- `+0x0C GetBufferPointer`: the vertex and pixel byte code before `CreateVertexShader` (0xa050df) and `CreatePixelShader` (0xa05164), and again for the cache writer 0x9f5d80 (0xa051c7, 0xa051e1).
- `+0x10 GetBufferSize`: for the cache writer (0xa051ba, 0xa051d4).
- The error buffer is only released, never read.

ID3DXConstantTable (vertex and pixel table, from both D3DX imports):
- `+0x24 GetConstantByName(table, NULL, name)`: 0x969f50 (loop over a global name table at 0xfc19c0, stride 0x368) and 0x93b090 (4 fixed names). The cache path calls 0x93b090 directly; the compile path calls 0x969f50, which tail-calls 0x93b090.
- `+0x18 GetConstantDesc(table, handle, &desc, &count)` at 0x96a041: reads `RegisterIndex` (+0x08, as a word), `RegisterCount` (+0x0C, as a byte), `Class` (+0x10), `Type` (+0x14, float and sampler types), `Elements` (+0x20, as a byte).
- `+0x08 Release`: compile path, 0xa05233 and 0xa05247. The cache path (0x9f6100) never releases its two tables.
- No `Set*` method is called anywhere in these paths.

## Profile choice (does the engine avoid ps_1_x on SM3 hardware?)

Yes, by default. 0xa2c8a0 reads the device caps (`VertexShaderVersion` 0x10b0fa0, `PixelShaderVersion` 0x10b0fa8) into the shader level at 0x1042f34: VS and PS >= 1.1 gives 1, >= 2.0 gives 3, >= 3.0 gives 6. Levels 3, 4 and 5 are then lowered to 2 (0xa2ca68..0xa2ca7c); level 6 stays. An override at 0xed8918 (initial value 0, written at 0x6993a5, 0x6993d8, 0x699e25, 0x699e59, 0x96fd5d) can force levels 1 to 6 through the table at 0xa2ccf4 (0 leaves the caps level).
0xa38c90 passes the level to the virtual 0x9ae920, which stores it at `+0x1060`; 0xa06bc0 passes `+0x1060` to 0xa04e20 as its 7th argument. 0xa04f6f maps it to profiles:

| Level | Vertex | Pixel | Flags |
|---|---|---|---|
| 1 | vs_1_1 | ps_1_1 | 0x10200 |
| 2 | vs_2_0 | ps_1_4 | 0x10200 |
| 3 | vs_2_0 | ps_2_0 | 0x200 |
| 4 | vs_2_a | ps_2_a | 0x200 |
| 5 | vs_2_a | ps_2_b | 0x200 |
| 6 | vs_3_0 | ps_3_0 | 0x200 |

Flags: 0x200 = `D3DXSHADER_AVOID_FLOW_CONTROL`, 0x10000 = `D3DXSHADER_USE_LEGACY_D3DX9_31_DLL`. When the byte at 0x1042e5c is set (0xa289c1), the pixel profile becomes ps_2_0.
So with SM3 caps and no override the game compiles vs_3_0/ps_3_0 only. An SM2-only device gets level 2, which is ps_1_4: report SM3 caps.

## Data the live non-shader calls pass (implemented in `runtime/src/d3dx_misc.c`)

- `D3DXCreateTextureFromFileInMemory` (0x9b0f55, `NuSSAOFilter::vfunc_03`): an 0x4012-byte TGA built on the stack: header type 2 (uncompressed true colour), no colour map, 64x64, 32 bpp, descriptor 0 (bottom row first), then 64x64 BGRA texels from 0xa6a1dd (rand-like) and two calls of 0x903240 on one angle per texel (alpha 0). Device from `[0x10b0ed4]`, texture to `0x1040ac8`, HRESULT to `0x101b674`. No DDS, PNG or other TGA kind reaches D3DX.
- `D3DXSaveSurfaceToFileInMemory` (0x9bb64e): `DestFormat` 0 (`D3DXIFF_BMP`), a `D3DPOOL_SYSTEMMEM` A8R8G8B8 offscreen surface filled by `StretchRect` + `GetRenderTargetData`, palette and rect NULL. The reader 0x9c0000 checks the buffer size against the BMP file size at +2 and reads 4-byte pixels from +54 (BITMAPINFOHEADER, 32 bpp). The second caller 0x9c2fe0 keeps the whole file at `0x1042f20`, its size at `0x1042f24`.
- Not confirmed against the native DLL: the BMP row order (written bottom-up, positive height), the box-filter rounding of the mip levels, and the summation order of `D3DXMatrixMultiply`.
