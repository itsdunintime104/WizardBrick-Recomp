# Shader oracle: real D3DXCompileShader calls from the original game

Result: the proxy capture works. `-DumpShaders` does not: it is dead code in the retail exe.

One unattended run of the original exe under Wine (front end only, no input) makes 386 `D3DXCompileShader` calls. Each call is stored with its source text, arguments, and the bytecode from Microsoft's own `d3dx9_41.dll` (Mar 2009 redist, x86).

## Counts (run of 2026-10-24, `out/summary.json`)

| Item | Value |
|---|---|
| Compile calls | 386 (193 VS + 193 PS) |
| Unique source texts | 286 |
| Unique (source, entry, profile, flags) | 286 |
| Unique bytecode blobs | 171 |
| Profiles | `vs_3_0` 193, `ps_3_0` 193 |
| Entry points | `vertexProgram` 193, `fragmentProgram` 193 |
| Flags | 0x200 on all 386 |
| D3DXMACRO / ID3DXInclude | none on all 386 (pDefines NULL, pInclude NULL) |
| HRESULT | S_OK on all 386 |
| Calls with a message buffer | 56, all warning X3571 (`pow(f, e)` with negative f) |
| Total compile time in D3DX | 16.0 s; all calls fall inside the first 40 s of a run |
| CachedShaders files written by the game | 188 + VERSION.DAT |

Runs 2 and 3 in the same prefix give the same 386 calls. `run.sh` deletes the prefix `CachedShaders` before each run, because a cache hit skips `D3DXCompileShader` (load path 0x9f6100, notes/shader_pipeline.md). A run with a warm cache was not tested.

The 188 cache names (`6xxxxxxxx.DAT`) share no name with the 336 developer cache names listed in GAME.DAT (all of those also start with level digit 6). So the hash in the name covers something that differs between the developer build machine and this run. Cause not checked.

## Where the dump lives

- `<work>/hp-shader-oracle/out/dump/<index>.hlsl`: exact `pSrcData`, `SrcDataLen` bytes.
- `<index>.json`: entry, profile, flags, defines, include flag, HRESULT, sizes, ms, thread.
- `<index>.bin`: `*ppShader` bytecode (with its CTAB). `<index>.err`: `*ppErrorMsgs` text.
- `<work>/hp-shader-oracle/out/cache/`: the game's own CachedShaders output.
- `<work>/hp-shader-oracle/out/summary.json`, `out/wine.log`.
- Game clone `<work>/hp-shader-oracle/game` (APFS clone, proxy DLLs copied in), prefix `<work>/hp-shader-oracle/prefix`, built DLLs `<work>/hp-shader-oracle/dll`.

## Commands

```sh
G="$HOME/.wine-steam-11/drive_c/Program Files (x86)/Steam/steamapps/common/LEGO Harry Potter"
O=<work>/hp-shader-oracle
cp -c -R "$G" $O/game
cabextract -d CABDIR "$G/redist/DirectX/Mar2009_d3dx9_41_x86.cab"   # d3dx9_41.dll, PE32 i386
tools/shaders/oracle_dump/build.sh CABDIR/d3dx9_41.dll $O/dll
tools/shaders/oracle_dump/run.sh $O 60          # seconds; makes the prefix on first use
python3 -I tools/shaders/oracle_dump/summarize.py $O/out/dump $O/out/summary.json $O/out/cache $O/game/GAME.DAT
```

`run.sh` sets `WINEPREFIX=$O/prefix`, `WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d;d3dx9_41=n;d3dx9ms=n"`, audio driver `""`, and runs `wine LEGOHarryPotter.exe -Windowed` from the game folder, then `wineserver -k` after the time. Default renderer (wined3d GL).

## Tools (`tools/shaders/oracle_dump/`)

- `proxy.c`: proxy `d3dx9_41.dll`. `hook_D3DXCompileShader` loads the real DLL (renamed `d3dx9ms.dll`), calls it, and writes the files above to `C:\shaderdump`. If the game passes NULL for `ppShader` or `ppErrorMsgs`, the proxy uses its own buffer and releases it.
- `gen_def.py`: forwards the other 335 exports to `d3dx9ms.<name>` at their original ordinals.
- `build.sh`: `i686-w64-mingw32-gcc` build; copies the real DLL next to it as `d3dx9ms.dll`.
- `run.sh`, `summarize.py`: as above.
- `compile_ms.c` (built by `build.sh` as `compile_ms.exe`): compiles one file with the real DLL and writes `.bin`, `.err`, and the `D3DXDisassembleShader` listing `.asm`.

## Why -DumpShaders does not work

- The argument parser (VA 0x970455..0x970680) compares each argument with `stricmp`-style 0x963940. For `-DumpShaders` (0xd19890) a match jumps straight to the loop end (0x9705f3 `je 0x970675`). No flag is set. `-Windowed` sets 0x1043304; `-HiresScreenshot` is also ignored.
- The dump routine starts at 0xa3c660 (`sub esp, 0x8c8`). It sets 0x10d4c54 = 1, gets the exe path (import 0x10de9f8, then 0x926310 dirname), appends `\filetable.txt`, opens it, reads lines of `"path"`, and dumps every path that contains `_nxg.gsc` or `_nxg.ghg` ("Dumping shaders for %s", 0xa3c7c1). No call, jmp, or 32-bit immediate in the file points to 0xa3c660. It is unreachable.
- So the filetable.txt format is known (quoted paths, one per line, next to the exe), but no command line reaches it. Running it would need a patched exe. Not done.
- Related: shader manager init at 0xa38c2c sets `C:\genShaders` on an object (vtable +8) and the include path `./shaders;../nu2api/shaderBuilder;C:\projects\nu2api\shaderBuilder` (vtable +4).

## Limits

- Coverage is the start-up and front-end set only: the run has no input, so no level loads. The exe has no level-select argument (options seen: `-openautomate -Fullscreen -HiresScreenshot -DumpShaders -Windowed /timeout: -NoFocus_Process -NoFocus_Pause -RelaunchMCE`). More scenes need input injection or a save file that starts in a level.
- The profile level is what wined3d GL caps on an M1 Max give (level 6, SM3). Other cap levels (vs_2_0, ps_2_b, flags 0x10200 at level < 3) are not covered.
- Run 1 (fresh prefix, 150 s) crashed with a read fault at 0x7BF21239 (inside kernel32.dll, loaded at 0x7BEB0000) before any compile. Runs 2 (40 s) and 3 (300 s) did not repeat it. Cause not checked.
- Game crash at 0x91B0AD (runs 3 and 4, about 36 s after XAudio2 start; all 386 compiles are done by then). Cause is in the game's audio path, not the proxy:
  - 0x91afe0 (reverb update) loads the submix voice from `[audio+0x392c]` and calls `SetVolume` (IXAudio2Voice vtable +0x30) and then `SetEffectParameters(0, params, 0x34, 0)` (+0x18) with no NULL check.
  - The only code that fills `[audio+0x392c]` is `IXAudio2::CreateSubmixVoice` at 0x95fde1 (XAudio2 2.4 vtable +0x24, called on `[audio+0x39c8]`). Its HRESULT is not checked. 0x95fdb0 has no direct caller (virtual).
  - Run 4 with `WINEDEBUG=err+all,trace+xaudio2,+timestamp`: `XAudio2CF_CreateInstance` (version 24), `Initialize`, `CreateMasteringVoice` (device index 0), `GetDeviceDetails`, one `CreateSourceVoice`. No `CreateSubmixVoice` line, and Wine traces that function on entry. So 0x95fdb0 never ran and the pointer stays NULL.
  - Not confirmed: why 0x95fdb0 is skipped. Best guess: with the audio driver set to `""`, the mastering voice has no device and the engine skips submix setup but still runs the reverb update. The check is one run with the audio driver on (sound plays on the host).
  - No backtrace: `winedbg --auto` output goes to its own console. AeDebug in this prefix is set to `cmd /c winedbg --auto %ld %ld > C:\crash.txt`, but the file only got `WineDbg attached to pid 00f0` before `wineserver -k`.

## Microsoft answer for the vkd3d E5021 (Inf/NaN) case

Command (from `<work>/hp-shader-oracle/dll`, same prefix, `WINEDLLOVERRIDES=...;d3dx9ms=n`):
`wine compile_ms.exe <src> <entry> ps_3_0 0x200 <out prefix>`. Output in `<work>/hp-shader-oracle/out/nan/`. Compiler banner: `Microsoft (R) HLSL Shader Compiler 9.26.952.2844` (d3dx9_41).

| Input | Flags | Result |
|---|---|---|
| `nan_min.hlsl` (`c.rgb *= 0.0; if (max3 > 1.0) c.rgb /= m;`), entry `main` | 0x200 | S_OK, 196 bytes, no message. Listing: `def c1, 0, 1, 0, 0` / `mov r0.w, c0.w` / `mul oC0, r0.w, c1.xxxy`. The branch is removed; no Inf/NaN error. |
| `6_ps.pp` (stand-in HDRToLDR permutation from shader-accept, already preprocessed), entry `fragmentProgram` | 0x200 | E_FAIL: `error X3650: 'vs_normal': global variables cannot use the 'half' type in ps_3_0` (line 139). Fails before any Inf/NaN check; this is a stand-in text error. |
| same | 0x1200 (adds ENABLE_BACKWARDS_COMPATIBILITY, diagnostic only) | S_OK, 136 bytes, 146 X3568 unknown-pragma warnings, no Inf/NaN message. Listing: `def c0, 0, 0, 0, 0` / `mov oC0, c0.x`. |

So native D3DX accepts the 0*x-then-divide pattern with no Inf/NaN error, and its output has no trace of the branch. vkd3d E5021 is stricter than native for this input. (Order of passes inside D3DX is inferred from the output, not read.) The game's own capture has no HDRToLDR or HDR_PRESERVESATURATION source (grep over the 386 `.hlsl` files), so no real game permutation of that shader was tested.
