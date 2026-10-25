# Function names for lhp_win32.exe

`recon/names.json` holds 9677 names for function starts in `recon/analysis.json`. Each entry has `name`, `source`, `confidence` and `evidence` (FID library, vtable and slot, string or table address, or saga literal).

Rebuild:

```
_JAVA_OPTIONS=-Xmx10g /opt/homebrew/Cellar/ghidra/12.1.4/libexec/support/analyzeHeadless \
  ~/Developer/ghidra_lhp lhp -process lhp_win32.exe -noanalysis -readOnly \
  -scriptPath tools/ghidra -postScript ExportNameEvidence.java <tmp>/evidence.json
python3 -I tools/pe/build_names.py <tmp>/evidence.json --saga <saga checkout>
```

The Ghidra run is read-only (`-readOnly`); the project is not changed, so no backup copy was made. saga is `github.com/opensagadev/saga` (shallow clone, read as data only; no code copied).

Symbolize a log: `python3 -I tools/pe/symbolize.py < crash.log`.

## Counts

| Source | high | medium | low | all |
|---|---|---|---|---|
| fid | 772 | 118 | 8 | 898 |
| string | 2 | 2024 | 0 | 2026 |
| rtti | 4269 | 397 | 1934 | 6600 |
| startup-trace | 0 | 51 | 0 | 51 |
| saga | 0 | 46 | 56 | 102 |
| total | 5043 | 2636 | 1998 | 9677 |

Rules (see the docstring of `tools/pe/build_names.py`):
- **fid**: high = one name from the Visual Studio 2005 libraries. Medium = several 2005 names (first one used; the evidence says how many), or one name from another version only. Low = several names, no 2005 match.
- **string**: high = a whole `Class::Method` string that only this function refers to, and the function refers to no other such string. Medium = a `{name string, function pointer}` record in a data table where the same record shape repeats at a fixed stride (script condition and action tables, config field tables).
- **rtti**: high = slot NN of the vtable of one class (`Class::vfunc_NN`), or a slot shared by classes that all derive from the named class. Medium = a slot whose first 48 bytes hold `test byte [esp+4],1` or `test byte [ebp+8],1` (`Class::scalar_deleting_dtor`). Low = a slot shared by unrelated classes (identical code folded by the linker, suffix `_sharedN`), or a non-slot function that stores exactly one class's vtable (`Class::ctor_or_dtor`).
- **startup-trace**: hand names for the addresses in `docs/STARTUP_REQS.md`; medium.
- **saga**: a string literal (8+ characters) used by exactly one function here and exactly one saga function; medium with 2+ agreeing literals, else low.
- When sources disagree, the higher confidence wins, then fid > string > rtti > startup-trace > saga.

Every "high" name is backed by FID, RTTI or an exact string.

## FunctionID database

Ghidra 12.1.4 ships no separate VS2005 file. `vsOlder_x86.fidbf` holds Visual Studio 1998, 2003, 2005, 2008 and 2010 (`Features/FunctionID/data/building_fid.txt`); the export queries all shipped x86 databases and records the library of each match. 904 functions matched; 867 have a VS2005 Release match. Names are demangled with `llvm-undname` and the parameter list is removed.

## 30+ names for boot debugging

| Address | Name | Source | Confidence |
|---|---|---|---|
| 0x00A69393 | WinMainCRTStartup | startup-trace | medium |
| 0x00A771FE | ___security_init_cookie | fid | high |
| 0x00A691B3 | __tmainCRTStartup | startup-trace | medium |
| 0x00A7712A | __heap_init | fid | high |
| 0x00A73E6E | __mtinit | fid | high |
| 0x00A75C3F | __ioinit | fid | medium |
| 0x00A76F52 | ___crtGetEnvironmentStringsA | fid | high |
| 0x00A76E99 | __setargv | fid | high |
| 0x00A6D301 | __cinit | fid | high |
| 0x00A76BBF | __wincmdln | fid | high |
| 0x00A217A0 | WinMain | startup-trace | medium |
| 0x009703D0 | ParseCommandLine | startup-trace | medium |
| 0x00A21630 | CheckGameExplorerAccess | startup-trace | medium |
| 0x006A7500 | GameMain | startup-trace | medium |
| 0x006A6C20 | GameInitHardware | startup-trace | medium |
| 0x00A3CA00 | NuInitHardware | startup-trace | medium |
| 0x0098FD20 | NuThreadCreate | startup-trace | medium |
| 0x00929E30 | SetThreadName | startup-trace | medium |
| 0x00A3B490 | StartRenderThread | startup-trace | medium |
| 0x00A3B3D0 | RenderThreadProc | startup-trace | medium |
| 0x0093C3E0 | NetInitWinsock | startup-trace | medium |
| 0x00A2C7E0 | D3DCreate | startup-trace | medium |
| 0x009F3FA0 | FileSystemInit | startup-trace | medium |
| 0x009B2610 | StartFileThreads | startup-trace | medium |
| 0x0098EA30 | ReadThreadProc | startup-trace | medium |
| 0x009A6CC0 | DecodeThreadProc | startup-trace | medium |
| 0x0092A020 | TimerCalibrate | startup-trace | medium |
| 0x00A2C8A0 | D3DCheckShaderModel2 | startup-trace | medium |
| 0x009EB2F0 | CreateGameWindow | startup-trace | medium |
| 0x009EAF50 | TTalesWindow_WndProc | startup-trace | medium |
| 0x009D42E0 | PumpMessages | startup-trace | medium |
| 0x009BFD20 | StartTextureDecodeThread | startup-trace | medium |
| 0x00A3A3E0 | D3DInitDevice | startup-trace | medium |
| 0x00A37D90 | D3DCreateDevice | startup-trace | medium |

## Hand-written CRT routines (FID names of VS2005 `crt/src/intel` assembly)

Assembly sources: `_memcpy` 0xA68680, `_memmove` 0xA69580, `_memset` 0xA67F90, `_memchr` 0xA6E450, `_strlen` 0xA6EDE0, `_strcmp` 0xA756F0, `_strncpy` 0xA6B420, `_strcspn` 0xA84300, `_strpbrk` 0xA87790, `__allmul` 0xA68570, `__alldiv` 0xA6E0A0, `__allrem` 0xA6A630, `__alldvrm` 0xA87D90, `__aulldvrm` 0xA69A60, `__aullrem` 0xA684C0, `__allshl` 0xA680C0, `__allshr` 0xA6E420, `__aullshr` 0xA698F0, `__chkstk` 0xA68540, `__alloca_probe_8` 0xA6E176, `__alloca_probe_16` 0xA6E160, `__SEH_prolog4` 0xA74290, `__SEH_epilog4` 0xA742D5, `__EH_prolog3` 0xA6EE6B, `__EH_prolog3_catch` 0xA6EE9E, `__EH_prolog3_GS` 0xA6EED4, `__EH_prolog3_catch_GS` 0xA6EF0A, `__EH_epilog3` 0xA6EF43, `__except_handler3` 0xA6B588, `__except_handler4` 0xA74320, `__global_unwind2` 0xA7C848, `__local_unwind2` 0xA7C8AD, `__local_unwind4` 0xA7D1B8, `__abnormal_termination` 0xA7C931, `__NLG_Notify` 0xA7C95D, `__NLG_Notify1` 0xA7C954, `__CallSettingFrame@12` 0xA74090.

Not confirmed as assembly: `_strstr` 0xA6A900 and `_strncmp` 0xA6CEFC (may be C in VS2005).

C or C++ with inline assembly (stack and frame tricks): `__VEC_memcpy` 0xA75B5C, `__VEC_memzero` 0xA74133, `_JumpToContinuation`, `_CallMemberFunction0/1`, `_CallSETranslator`, `_UnwindNestedFrames`, `_CallCatchBlock2`, `___CxxFrameHandler`, `__CxxThrowException@8`.

Not found by FID: `_ftol2`, `_ftol2_sse` and the `_CI*` x87 math helpers. They are short, so FID scores them under its threshold. Open: find them by byte pattern.

## Function-boundary problems (not changed)

- 6797 entries in `recon/analysis.json` are Ghidra functions named `caseD_*` (6462) or `__break` (335): switch-case and post-switch labels that Ghidra made into functions. They split the bodies of the real functions, so `symbolize.py` leaves an address inside one of these pieces unnamed instead of naming the enclosing function.
- 7 of these pieces got a name. Two are vtable slots, so they are real entries: 0x417325 `TileMapGeom::vfunc_28` and 0x53575F `MirrorAgent<SplineAgent, Geom, SplineGeom>::vfunc_07`. The other five are not proof of an entry: 0x603763 `NodeGroupPacket::ctor_or_dtor` only stores a vtable, and 0x6D8998, 0x6D89A0, 0x6D89A8 `GizmoForce_GetOutputName` and 0x6E5470 `GizmoObstacle_GetOutputName` are table hits 8 bytes apart that may be a wrong table pairing. Not checked further. The other 6790 pieces were not checked one by one.
- 0xA37730 (SoundEvent thread start in `docs/STARTUP_REQS.md`) is not a function start in `recon/analysis.json`; it got no name.
- 0xA69393 is named `WinMainCRTStartup` (the MSVC name for a WinMain program); `docs/STARTUP_REQS.md` calls it mainCRTStartup.
