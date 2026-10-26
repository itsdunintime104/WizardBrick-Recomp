# LEGO Harry Potter Years 1-4: Windows exe to native arm64 macOS

Static recompilation of the Windows PC build, using the same engine as
HobbitRecomp and MarvelRecomp (MacRecomp). No Wine, no emulation, no Rosetta at
run time.

## What is new

The two shipping games started from Feral Interactive's **Mac** i386 binaries,
so their host side already existed: POSIX, Core Audio, OpenGL, SDL2. This game
starts from a **Windows** binary, so two host layers do not exist yet:

| Layer | Size of the job | Why |
|---|---|---|
| Lifting x86-32 to C | already built | Same instruction set as the i386 Mach-O games. The lifter's upstream (xboxrecomp) was written for MSVC x86 code in XBE files, which is what this exe is. |
| PE front end | small | Replaces the Mach-O analyzer: sections, entry, imports, function boundaries. |
| Win32 imports | medium, mechanical | 231 functions over 14 DLLs. Most map to libc, pthreads, the file system and SDL2. |
| Direct3D 9 + D3DX | **the real work** | The engine renders through D3D9 and compiles HLSL at run time. Nothing in the current engine speaks D3D9. |

## Measured facts

Binary: `orig.nosync/lhp_win32.exe`, sha256 `21c2d30c…`, 14,598,144 bytes, a
copy of the Steam install (app 21130).

- PE32 i386, MSVC 2005, linked 2010-06-01, subsystem GUI, image base 0x400000,
  entry 0x00a69393. No relocation directory, so the image must load at its
  fixed base and no relocation pass is needed.
- Not packed and not Steam-DRM wrapped: `.text` entropy 5.83, the import table
  is plain, there is no `.bind` section.
- Sections: `.text` 0x401000 vsize 0x841946; `.rdata` 0xc43000 vsize 0x1bf6d1;
  `.data` 0xe03000 vsize 0x2da0f0 with a large zero-filled tail (raw 0x112000);
  `.idata` 0x10de000; `.shr`; `CONST`; `.rsrc` 0x10e3000 vsize 0x2d156c.
- Most code ends at 0xac3787, followed by 0xCC linker padding, but the
  static initialisers sit after it: `.text` code really ends at 0xC415E2
  (found 2026-10-24, notes/stub_triage.md). Compute the code end from the
  bytes; do not use 0xac3787 as a limit.
- Imports: 231 functions over 14 DLLs — KERNEL32 142, USER32 29, WS2_32 22,
  d3dx9_41 13, ole32 5, ADVAPI32 4, GDI32 3, SHELL32 3, XINPUT1_3 3,
  OLEAUT32 2, X3DAudio1_6 2, DINPUT8 1, comdlg32 1, d3d9 1
  (`Direct3DCreate9` only: every other D3D9 entry is reached through COM
  vtables).
- PDB name in the debug directory: `legogame_Final_PC.pdb`, build tree
  `lhp-pc-sub1-28-05-10-v11.9k_hotfix_2847657`. The PDB itself is not shipped.
- The engine is TT Games "nu2", the same engine family as The Hobbit and
  Marvel, and the same as the public `saga` decompilation. Class names in the
  binary include `NuShaderManager`, `NuShaderObjectPC`, `HLSLShaderBuilder`,
  `ShaderBuilderGen`.

## Route

1. **M0 front end and measurement.** A PE analyzer writes the same
   `analysis.json` the Mach-O analyzer writes, so the sweeper, the switch-table
   finder and the lifter work unchanged. Done: `engine/windows/tools/pe/analyze_pe.py`.
   Function boundaries come from Ghidra, because a stripped PE has no
   function-starts table. Measure: functions lifted, functions failed,
   untranslated instructions, unset-flag reads.
2. **M1 link.** Every lifted function compiles and links for arm64 with stub
   imports. This is the gate the two shipping games passed before anything ran.
3. **M2 run-time loader.** Map the PE at 0x400000, zero-fill the `.data` tail,
   bind the IAT to host thunks. Done: `engine/windows/runtime/src/pe_load.c`.
4. **M3 Win32 thunks.** Work through the 231 imports by bucket: file system and
   paths, threads and synchronisation, memory, the CRT's environment and
   locale, the window and message loop (SDL2), the registry entries the game
   reads for its install path and language, input (XInput, DirectInput8),
   audio (X3DAudio plus the engine's own mixer), and stubs for Winsock.
5. **M4 renderer.** See the decision below. The game must reach its first
   rendered frame before anything else is tuned.
6. **M5 play.** Start-up, menus, save loading, then a level, with the existing
   checks: `recomp-check` (static) and `boot-check` (unattended run).
7. **M6 package.** The same `setup.sh` and app bundle the other two games use.

## The renderer

Measured first (`notes/shader_pipeline.md`), which cut this job down:

- The game does **not** use the D3DX effect framework. `D3DXCreateEffectFromFileA`,
  `D3DXCreateEffect` and `D3DXCreateEffectCompiler` are imported but sit in
  wrappers with no callers and no pointers to them. So no `.fx` runtime, and
  none of Wine's 7,000-line `effect.c` is needed.
- Only two D3DX entries are live: `D3DXCompileShader` (callers at 0xa05007 and
  0xa0506b) and `D3DXGetShaderConstantTable` (callers at 0x9f661c and 0x9f6629).
- The exe already carries 451 compiled D3D9 shader blobs between 0x88c678 and
  0x917390: ps_3_0 x337, vs_3_0 x32, ps_2_0 x31, vs_1_1 x28, vs_2_0 x13,
  ps_1_1 x10. Code at 0x9a3307 hands them to `IDirect3DDevice9::CreateVertexShader`
  (vtable offset 0x16c).
- It also carries 283,641 bytes of HLSL source in `.data`, including a 213,219
  byte uber shader at file offset 0xa9bed0 that is specialised from about 140
  `#define` axes and compiled at run time, then cached to `%d%08x.DAT`.
  GAME.DAT's file table lists 342 `CachedShaders\*.DAT` members.

So the renderer needs a D3D9 device and resources, a compiler for HLSL to
Shader Model 1-3 byte code, and constant-table reflection. Nothing more.

### Decision (2026-10-24): DXVK Native on KosmicKrisp

Proven on this Mac (`notes/dxvk_triangle.md`): upstream
DXVK (`02e2028c`) plus four published macOS patches (`engine/windows/tools/dxvk_test/patches`),
built D3D9-only, on KosmicKrisp from Homebrew Mesa 26.2.5. Clear, a
vertex-coloured triangle and a textured quad read back bit-exact; 300 presents
at a median of 8.2 ms.

MoltenVK is ruled out: it lacks `nullDescriptor`, on which DXVK's binding
model depends, and `shaderCullDistance` and `robustBufferAccess2` besides.

Shaders: `D3DXCompileShader` on vkd3d-shader 2.1 (HLSL to SM1-3 byte code;
registers match the game's own compiled blobs), and a hand-written CTAB reader
for `ID3DXConstantTable` (`notes/vkd3d_test.md`). The game ships no shader
cache (`notes/shader_cache.md`), so the compiler runs at run time.

The D3D9-to-OpenGL fallback with Cg is no longer needed.

DXVK uses its SDL2 window backend, the same SDL as the engine and the UI
slice (`engine/windows/runtime/src/d3d9_backend.c`; the first triangle test used SDL3).

## Rules carried over

- Derive every per-game value from the binary. Never type one in.
- Root cause first: name the line and why it fails, with evidence. No guards or
  retries that hide a symptom.
- Reuse before writing. Look for an existing tool first.
- The game binary and the `.DAT` archives are data. Parse them, never run them.
