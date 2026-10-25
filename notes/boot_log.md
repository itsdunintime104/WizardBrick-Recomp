# Boot log: first runs of the recompiled game

Driver: `runtime/src/hp_boot.c` (`int hp_boot_run(const char *exe_path, const char *game_dir)`:
the image is mapped from `orig.nosync/lhp_win32.exe`, the game folder is the
Steam folder, so the two paths are separate arguments) and
`runtime/src/hp_boot_main.c` (fatal-signal report, then `hp_boot_run`;
`argv[1]`/`argv[2]` override both paths). `HP_TRACE_IMPORTS=N` logs the first
N import calls (`IMP n name(arg0..arg3) ret <guest ret> -> eax`). Build: `tools/boot/build_boot.sh [GEN]`
into `~/Developer/hp-boot-build/lhp_boot`. Run:
`tools/boot/run_boot.sh <N> [VAR=value ...]` (owner run rules: 120 s alarm,
KosmicKrisp ICD, `DXVK_WSI_DRIVER=SDL2`, `RECOMP_ASSUME_FOCUS=1`,
`RECOMP_NO_ALERT_UI=1`, `DXVK_LOG_LEVEL=info`); logs in
`<job tmp>/boot/run<N>.log`.

Boot sequence (`hp_boot_run`): `guest_mem_init`, `guest_heap_init`,
`pe_load_image(win32_resolve_import)`, code range from the PE section table
(`g_xbox_code_lo/hi` = executable sections), `w32_fs_set_game_dir(Steam dir)`,
`w32_fs_set_save_dir(~/Library/Application Support/HarryPotterRecomp)`,
`w32_ui_set_window_flags(SDL_WINDOW_VULKAN)`, `recomp_dispatch_init`,
`diag_init`, main guest stack 0xFD000000-0xFE000000, x87 CW 0x027F, MXCSR
0x1F80, `w32_tib_attach(0xFE000000, 0xFD000000)`, push sentinel return
0xFFFFF000, call `recomp_lookup(0xA69393)`.

Fatal report: signal, fault address, guest offset, `g_esp`, `[esp]`, `fs`,
last 8 icall targets, host backtrace (lifted functions appear as
`sub_<VA>`, linked with `-export_dynamic`).

## Run 1 (generated_f2f0e46_19k, all TSO=1 except lhp_0038 TSO=0, O1)

Command: `tools/boot/run_boot.sh 1 RECOMP_STARTUP_VERBOSE=1`

```
hp_boot: image 0x00400000-0x013B5000 entry 0x00A69393 unbound 0
hp_boot: code 0x00401000-0x00C42946
hp_boot: calling entry 0x00A69393
FATAL signal 10 (Bus error: 10) addr 0x3fffffffc guest_off 0xfffffffc g_esp 0xFDFFFF68 [esp] 0x78DB94BF fs 0x200003C0
icalls: 0xFE0006F0 0xFE000490 0xFE0005B0 0xFE000480 0xFE000470 0x00000000 ...
2   lhp_boot  sub_00A691B3 + 148
3   lhp_boot  hp_boot_run + 648
```

Reached: `__security_init_cookie` (5 imports), then `__tmainCRTStartup`
0xA691B3. Failure 1 below.

### Failure 1: SEH4/EH3 frame helpers do not hand `ebp` back (lifter)

- Fault: read of guest 0xFFFFFFFC in `__tmainCRTStartup` 0xA691B3 at
  0xA691BF `and dword ptr [ebp-4], 0`, with `ebp` = 0.
- Cause: 0xA691B3 calls `__SEH_prolog4` (0xA74290), which builds the
  caller's frame (`lea ebp, [esp+0x10]` at 0xA742A4). The lifted caller keeps
  `ebp` in a C local and never reads it back after the call
  (`generated_f2f0e46_19k/lhp_0037.c`, `sub_00A691B3`: no `ebp = g_seh_ebp`
  after `RECOMP_ABI_CALL(0x00A74290u, ...)`). The engine skips helper
  detection for every non-XBE image: `engine/tools/xrecomp/translator.py:3218-3219`
  (`if image_format != "xbe": seh_prolog, seh_epilog = (), None`). Detection
  would also miss them: `lifter.py:1205-1207` only knows the
  `mov eax, fs:[0]` / `leave; push ecx; ret` forms, and `__SEH_prolog4` uses
  `push fs:[0]` and `mov fs:[0], eax`, `__SEH_epilog4` ends `mov esp, ebp;
  pop ebp; push ecx; ret` (disassembly 0xA74290..0xA742E8).
- Owner: lifter, already planned as `docs/SEH_PLAN.md` slice 1; fix committed
  in `~/Developer/MacRecomp-hp-seh` `c127626` (helpers passed
  by `tools/pe/run_lift_pe.py --seh-prolog/--seh-epilog`).
- Workaround for run 2: TEMPORARY splice of the SEH lane's partial lift
  (`tmp/seh_lift`, 185 functions) into a copy of the base lift
  (`tmp/boot/py/splice.py`, output `tmp/boot/gen_seh`).

## Run 2 (gen_seh: f2f0e46 base + SEH lane's 185 functions)

Command: `tools/boot/run_boot.sh 2 RECOMP_STARTUP_VERBOSE=1` (exit 3)

```
GetProcAddress KERNEL32.dll!FlsAlloc -> 0x00000000
... FlsGetValue, FlsSetValue, FlsFree, EncodePointer, DecodePointer,
    InitializeCriticalSectionAndSpinCount -> 0x00000000
unimplemented at 0x00A6E152: repz ret
RECOMP: caller=0x00A825B9 esp=0xFDFFF85C icalls=[...]
```

Reached: past `_heap_init`, `_mtinit`, `_mtinitlocks` (CRT falls back to TLS
as designed).

### Failure 2: `repz ret` not lifted (lifter, already fixed)

- Cause: the f2f0e46 lifter has no `repz ret` form (`lift_report.json`
  `unhandled_top`: `repz ret` 1), so 0xA6E152 is `recomp_unimpl`, which exits 3.
- Owner: lifter-gaps; fixed in `MacRecomp-hp` `3662e2b` ("Lift repz ret as
  ret"); `generated_clone_19k` has 0 unhandled instructions.
- Fix for run 3: full lift of the same 19,466-function analysis with
  `MacRecomp-hp` HEAD (`f7ce7e6`) plus the SEH commit `c127626` applied, in a
  scratch copy (`tmp/boot/eng_merge`), through `tools/pe/run_lift_pe.py`.
  Output `tmp/boot/gen_merge`. No lane's files were edited.

## Run 3 and 4 (gen_merge: MacRecomp-hp f7ce7e6 + SEH c127626, 19,466 functions)

Build: `ENGINE=tmp/boot/eng_merge tools/boot/build_boot.sh tmp/boot/gen_merge`.
The runtime must be built from the same engine as the lift: MacRecomp-hp adds
`ts_eflags_id` to the thread state (`recomp_types.h`) and `recomp_cpuid` to
`recomp_glue.c`; the f2f0e46 runtime fails to link (`_recomp_cpuid`
undefined) and would disagree on the thread-state layout.

Check A of SEH_PLAN slice 1 on this lift: 61 `RECOMP_ABI_CALL(0x00A74290u`
sites, 61 followed by `ebp = g_seh_ebp`.

Command: `tools/boot/run_boot.sh 3 RECOMP_STARTUP_VERBOSE=1` (exit 255), then
`tools/boot/run_boot.sh 4 HP_TRACE_IMPORTS=100000` (import trace, new in
`hp_boot.c`).

```
MessageBoxA: Microsoft Visual C++ Runtime Library: Runtime Error!
Program: C:\Game\LEGOHarryPotter.exe
R6009
- not enough space for environment
```

Trace (filtered):

```
IMP 187 GetACP(...) ret 00A80740 -> 000004E4
IMP 188 HeapAlloc(20001440 00000000 00000220 20001470) ret 00A6A8AE -> 20001EC0
...
IMP 229 GetModuleFileNameA(20001EC0 010DB378 00000104 00000000) ret 00A76EC9 -> 00000000
```

Reached: CRT `_ioinit`, `GetCommandLineA`, `__crtGetEnvironmentStringsA`,
`__initmbctable` -> `_setmbcp`, then `_setargv`.

### Failure 3: SEH4 function tails are stubs, so `__SEH_epilog4` never runs (function list)

- Symptom: `_setargv` (0xA76E99) does `xor ebx, ebx` (0xA76EA0), calls
  `__initmbctable` (0xA76EAC), then pushes `ebx` as `hModule` (0xA76EBC). The
  trace shows `hModule` = 0x20001EC0, the 0x220-byte block `_setmbcp`
  allocated (IMP 188). `GetModuleFileNameA` fails and the CRT stops with R6009.
- Cause: `_setmbcp` (0xA8097F) saves ebx/esi/edi in `__SEH_prolog4` and
  restores them in `__SEH_epilog4`, which it reaches only through its tail
  pieces 0xA80AEB, 0xA80B0C, 0xA80B10 (`mov eax,[ebp-0x20]; call 0xA742D5`).
  The 19,466-function list stops `_setmbcp` at 0xA80AE0 (size 353), so the
  lifted body tail-calls those three addresses, and they are unresolved stubs
  that only do `g_esp += 4` (`gen_merge/lhp_stubs_unresolved.c:58-60`). ebx,
  esi, edi, esp and ebp are never restored.
- Owner: function list (lift-review C group, `notes/lift_review.md:33,95,98`;
  SEH_PLAN slice 2; stub-triage CFG list task 9 has 0xA80AEB and 0xA80B10 as
  `tailjmp` starts but not 0xA80B0C).
- All 48 unresolved-call stubs of the lift have the same risk (each returns
  without running the code at its address).
- Workaround for run 5: TEMPORARY scratch analysis (`tmp/boot/lift_in2`) =
  the 19,466 list plus the 48 stub targets as starts (size from the CFG list
  when present, else up to the next start; `tmp/boot/py/add_starts.py`),
  lifted with `--only` into `tmp/boot/gen_add`, spliced into a copy of
  gen_merge (`tmp/boot/gen_fix`).

## Run 5 (gen_fix: gen_merge + the 48 stub targets lifted as functions)

Command: `tools/boot/run_boot.sh 5 HP_TRACE_IMPORTS=100000 RECOMP_STARTUP_VERBOSE=1` (exit 3)

```
IMP 229 GetModuleFileNameA(00000000 010DB378 00000104 00000A28) ret 00A76EC9 -> 0000001B
...
GetProcAddress KERNEL32.dll!IsProcessorFeaturePresent -> 0x00000000
indirect call to unresolved VA 0x00A68198
RECOMP: caller=0x00A6D161 esp=0xFDFFFF4C icalls=[0x00A68198 0xFE0010F0 ...]
```

Failure 3 is gone: `_setargv` passes `hModule` 0 and `_setenvp` succeeds.
Reached: `_cinit` (0xA6D301) -> `_initterm_e` over `__xi_a..__xi_z`.

### Failure 4: initializers and other data-referenced functions are not in the function list (function list)

- Cause: the first `__xi` entry, 0xA68198 (`push esi; push 4; push 0x20;
  call 0xA719F6`, real code), is not a lifted function, so
  `recomp_lookup` fails and `recomp_icall_fail_log` exits 3 by design.
  Count (`tmp/boot/py/initcov.py`):

  | List | `__xi` missing (of 6) | `__xc` missing (of 284) |
  |---|---|---|
  | `tmp/analysis_19466.json` (lifted) | 5 | 242 |
  | `tmp/stub_triage/analysis_cfg.json` (task 9) | 0 | 242 |
  | `recon/analysis.json` (full) | 0 | 242 |

  The 242 are C++ dynamic initializers between 0xC3B000 and 0xC42946 (for
  example 0xC3EAA8: `mov ecx, 0x10DAD59; call 0xA6728C; push 0xC4156A;
  call 0xA68203; pop ecx; ret`), including the two that STARTUP_REQS names
  as boot-relevant (0xC3E8C0 registry read, 0xC3E3E0 semaphores). They are
  real code: `docs/PLAN.md` "Real code ends at 0xac3787" is wrong for that
  range.
  The 19,466 list also lacks the boot thread start routines (Render
  0xA3B3D0, m_readThread 0x98EA30, m_decodeThread 0x9A6CC0), the
  EnumDisplayMonitors callback 0x945080, the unhandled-exception filter
  0xA769C8 and the `atexit` function 0xA770AB; the CFG list has all six.
- Owner: function list (`tools/pe/analyze_pe.py`, stub-triage task 9).
  Proposed fix: seed a function start at every non-null pointer in
  `__xi_a..__xi_z` (0xC43964..0xC43C84) and `__xc_a..__xc_z`
  (0xC43000..0xC43860), and lift with the CFG list (data, call, tailjmp,
  imm, backjump evidence) instead of the 19,466 list.
- Workaround for run 6: TEMPORARY scratch list `tmp/boot/lift_in3`
  (`tmp/boot/py/scratch_list.py`): CFG list without `orphan` entries, plus
  the 19,466 entries it lacks, plus all `__xi`/`__xc` pointers, plus the 48
  stub targets: 27,590 functions. Full lift into `tmp/boot/gen_cfg`.

## Run 6 (gen_cfg: scratch list, 27,590 functions, 30,950 lifted)

Build note: with this list the TSO=1 `bts` compile bug moved from lhp_0038
to lhp_0053 (`GLD32(...) = (GLD32(...) | ...); /* bts */`, "expression is
not assignable"). `build_boot.sh` now compiles at TSO=0 any chunk that has
that line, instead of naming lhp_0038. Owner: lifter (fixed in
MacRecomp-hp-review `22fd5b8`, taken back out of MacRecomp-hp in `f7ce7e6`).

Command: `tools/boot/run_boot.sh 6 HP_TRACE_IMPORTS=100000 RECOMP_STARTUP_VERBOSE=1` (exit 3)

```
IMP 505 SetUnhandledExceptionFilter(00A769C8 ...) ret 00A76A10 -> 00000000
IMP 522..525 InitializeCriticalSection(...) ret 00A67100
...
indirect call to non-code VA 0x656D6954
RECOMP: caller=0x00A6D363 esp=0xFDFFFF44 icalls=[0x656D6954 0x00C3AC60 0x00C3ABB0 ...]
```

Failure 4 is gone: all 6 `__xi` initializers ran. Reached: the `__xc`
loop in `_cinit` (0xA6D35B..0xA6D368, `mov eax,[esi]; call eax; add esi,4`).

Not a new blocker: my own scratch list cut 0xC3AC60 at the 0x400 size cap
(real end: first `ret` at 0xC3B553, size 2292). The lifted body ended
before `pop edi; pop esi; pop ebx`, so `esi` came back as 0xC45048 and the
loop read "Time" (0x656D6954) as the next pointer. Fixed in my scratch list
(`scratch_list.py`: an added start with no jump before its first `ret` ends
there; otherwise it runs to the next start, no cap), relifted the 277 added
starts with `--only` (`tmp/boot/gen_add4`) and spliced them into
`tmp/boot/gen_cfg2`. The function-list owner needs the same rule: size
from the code, not from the next start.

## Run 7 (gen_cfg2: run 6 list with code-based sizes for the 277 added starts)

Command: `tools/boot/run_boot.sh 7 HP_TRACE_IMPORTS=100000 RECOMP_STARTUP_VERBOSE=1` (exit 2)

```
IMP 3277..3284 SystemParametersInfoA(3A/34/32/3B/35/33/10/11 ...) -> 1
IMP 3285 SetThreadExecutionState(80000003 ...) ret 00A21818 -> 80000000
IMP 3286 CoInitializeEx(00000000 00000002 ...) ret 00A21827 -> 00000000
IMP 3287 GetModuleHandleA(00000000 ...) ret 009703EF -> 00400000
IMP 3288 GetModuleFileNameA(00400000 010D6A28 00000104 ...) ret 009703F6 -> 0000001B
IMP 3289 CoInitialize(00000000 ...) ret 00A2166B -> 00000001
IMP 3290 CoCreateInstance(00D1C528 00000000 00000001 00D1C538) ret 00A2168B -> 80040154
IMP 3291 CoUninitialize(...) ret 00A21733
IMP 3292 GetCurrentThread(...) ret 00A2184F -> FFFFFFFE
IMP 3293 SetThreadAffinityMask(FFFFFFFE 00000001 ...) ret 00A21856 -> 000000FF
FATAL signal 10 (Bus error: 10) addr 0x300000e40 guest_off 0xe40 g_esp 0xFDFFFF3C ...
2   lhp_boot  sub_006A7500 + 144
3   lhp_boot  sub_00A217A0 + 3028
4   lhp_boot  sub_00A6931C + 144
```

Reached: all CRT start-up, all 284 C++ initializers (2,790 import calls,
including the 0xC3E3E0 semaphores), and WinMain 0xA217A0 up to the game main
call at 0xA21862: the accessibility SystemParametersInfoA calls, the
command line (0x9703D0), and the permission check. STARTUP_REQS OPEN item
confirmed: CoCreateInstance returns REGDB_E_CLASSNOTREG (0x80040154) and
WinMain continues (no "Insufficient Permissions" box).

### Failure 5: incremental-link thunks reached only from initializers are stubs (function list)

- Fault: game main 0x6A7500 reads `ecx = [0xF54CD8]` (0x6A7514) and stores
  to `[ecx+0xE40]` (0x6A751E); `[0xF54CD8]` is NULL.
- Cause: the only writer of 0xF54CD8 is 0x6926E0 (`GST32(0xF54CD8, esi)`,
  gen_cfg2 `lhp_0022.c:66954`), the base constructor called from the app
  constructor 0x5349A0. Its only caller is the incremental-link thunk
  0x40366B (`jmp 0x5349A0`), called from the initializer 0xC3BCC0. 0x40366B
  is not a function in the list, so it is an unresolved stub (`g_esp += 4`),
  and the app object is never constructed.
- Owner: function list. The CFG list has 3,344 `jmp rel32` thunks of 5
  bytes (0x401005..0xAC3612) but marks them `orphan` because their callers
  (the initializers) were missing; 1,728 of them are in no lifted list.
  Proposed fix: add every incremental-link thunk as a function (or have the
  lifter follow `call <jmp rel32>` to the target), after the initializers
  are seeded (failure 4).
- Workaround for run 8: TEMPORARY second round (`tmp/boot/py/add_round.py`):
  all 165 unresolved-call stub targets of gen_cfg2 added as starts
  (`tmp/boot/lift_in5`), lifted with `--only` (1,216 functions,
  `tmp/boot/gen_add5`), spliced into `tmp/boot/gen_cfg3`.

## Run 8 (gen_cfg3: run 7 list plus the 165 stub targets of round 2)

Command: `tools/boot/run_boot.sh 8 HP_TRACE_IMPORTS=100000 RECOMP_STARTUP_VERBOSE=1` (exit 3)

```
IMP 3338 CreateThread(00000000 00010000 00967410 00ED6E48) ret 0098FDD3 -> FF800024
IMP 3353 Direct3DCreate9(00000020 ...) ret 00A2C7EB -> 200021B0
IMP 3902 RegisterClassExA(00FF1C44 ...) ret 009EB3A2 -> 0000C000
IMP 3904 CreateWindowExA(00000000 00D1BD94 00C8C250 00CF0000) ret 009EB45F -> 00010010
IMP 3919 ShowWindow(00010010 0000000A ...) ret 009EB4A1 -> 00000001
IMP 6939 RaiseException(406D1388 00000000 00000004 ...) ret 00929E83 -> 00000000
info:  D3D9DeviceEx::ResetSwapChain: ... Width 960 Height 600 A8R8G8B8 D24S8 Windowed false
info:  Presenter: Actual swapchain properties: B8G8R8A8_UNORM IMMEDIATE 1920x1200, 3 images
IMP 7038 DirectInput8Create(00400000 00000800 00D2C684 00ED7F70) ret 009B45D9 -> 00000000
UNSUPPORTED dinput8: IDirectInputDevice8A::SetProperty 0x2 (guest return 0x009B4673)
IMP 7040 SHGetFolderPathA(00000000 0000001A ...) ret 009452A7 -> 00000000
IMP 7066..7068 CreateDirectoryA(...) -> 0
IMP 7075 CreateFileA(010D5728 80000000 00000003 ...) ret 00A8F96D -> FFFFFFFF
indirect call to unresolved VA 0x0096D7EB
RECOMP: caller=0x000000FF esp=0xFDFFF854 icalls=[0xFE001070 ...]
```

Reached: `NuInitHardware` through the window ("TTalesWindow" class,
CreateWindowExA, ShowWindow), the boot threads (CreateThread with the
0x967410 trampoline; the thread-name RaiseException 0x406D1388 continues),
Direct3DCreate9 and CreateDevice on DXVK (swap chain created), DirectInput8,
and the profile folder. Not reached: the PeekMessageA pump (0 PeekMessageA
calls in the trace), so no frames.

### Failure 6: one-arm byte-map switch lifted as an indirect tail jump (lifter, already fixed)

- Cause: 0x96D690 ends a loop body with `cmp ecx, 6; ja 0x96D7F1;
  movzx ecx, byte [ecx+0x96D82C]; jmp [ecx*4+0x96D828]` (0x96D7D8..0x96D7E4).
  The table at 0x96D828 has one entry (0x96D7EB) and the map bytes for
  0..6 are all 0. The lifter's two-arm rule rejects it, so it becomes
  `RECOMP_ITAIL(GLD32(ecx * 4 + 0x96D828))` (gen_cfg3, `sub_0096D690`), and
  0x96D7EB is not a function: `recomp_icall_fail_log` exits 3.
- Owner: lifter; fixed in MacRecomp-hp-review `bacac14` ("Accept a one-arm
  byte-map switch when its bound and map prove the arm"), which is not in
  MacRecomp-hp `f7ce7e6` (the engine of this lift).
- Next step: relift with an engine that has both `bacac14` (review fixes)
  and `c127626` (SEH helpers), and a function list with failures 3-5 fixed.

### Not blocking, for owners

- `UNSUPPORTED dinput8: IDirectInputDevice8A::SetProperty 0x2` (guest
  return 0x9B4673): DIPROP_BUFFERSIZE. Owner: w32-input.
- DXVK `ExtEscape: escape 29446 not supported` comes from the game's
  CreateDCA/ExtEscape(0x7306) probe at 0xA417A3; it returns 0 and boot goes on.
- The game asks for a full-screen 960x600 device (`Windowed false`) because
  no `pcconfig.txt` exists yet (CreateFileA -> INVALID_HANDLE_VALUE).

## Summary

| # | Failure | First seen | Owner | State |
|---|---|---|---|---|
| 1 | SEH4/EH3 frame helpers do not hand `ebp` back; `[ebp-4]` with ebp 0 in `__tmainCRTStartup` | run 1 | lifter (SEH_PLAN slice 1) | fixed in MacRecomp-hp-seh `c127626`, not merged |
| 2 | `repz ret` unimplemented (0xA6E152) | run 2 | lifter | fixed in MacRecomp-hp `3662e2b` |
| 3 | `_setmbcp` tail pieces are `g_esp += 4` stubs; ebx leaks into `_setargv`, R6009 | run 3 | function list (SEH4 pieces, lift-review C group) | open |
| 4 | `__xi`/`__xc` initializers not lifted (5/6 and 242/284 missing) | run 5 | function list | open |
| 5 | incremental-link `jmp` thunk 0x40366B is a stub; app object 0xF54CD8 never built; NULL+0xE40 in GameMain | run 7 | function list | open |
| 6 | one-arm byte-map switch in 0x96D690 lifted as ITAIL to 0x96D7EB | run 8 | lifter | fixed in MacRecomp-hp-review `bacac14`, not merged |

Scratch workarounds (all under `<job tmp>/boot/`,
none in a lane's files): `eng_merge` (MacRecomp-hp f7ce7e6 + c127626),
`py/scratch_list.py` and `py/add_round.py` (function-list rounds),
`py/splice.py` (replace function bodies from a partial `--only` lift),
`py/run_lift_pe.py` (copy of the runner without `--entry-zf`, which
eng_merge does not know). Last binary: gen_cfg3, 27,755 listed functions.

## Trace format and golden-trace divergences (run 9, gen_cfg3)

`HP_TRACE_IMPORTS=N` now writes the `notes/golden_trace.md` format:
`IMP <n> t=<tid> <DLL>!<Func>(<args>) ret <RET> -> <EAX>` and
`THR <tid> <name>`. n is taken at entry; tid is TIB+0x24 (what
GetCurrentThreadId returns). The argument count is the number of slots
the thunk popped, measured from `g_esp`, so a cdecl import prints no
arguments. Slots are read only below the thread's stack base (TIB+4),
because the first calls run near the top of the main stack and the
memory above it is unmapped (the first version faulted there). Every
non-zero address that GetProcAddress returns gets a trampoline named
`<module>!<name>`; the module name comes from traced
GetModuleHandleA/LoadLibraryA calls. In trace mode, GetProcAddress returns
the trampoline's address, not the thunk's own. The pool has 512 slots.

Run 9: `tools/boot/run_boot.sh 9 HP_TRACE_IMPORTS=1000000` (same failure 6
as run 8; 7,087 IMP lines; THR lines for Render, m_readThread,
m_decodeThread, TextureDecode). Compare:
`compare.py golden.jsonl run9.log --thread main`: 308 of our 371
main-thread events match.

Run-script changes asked for by packaging and the lead: the final link has
`-Wl,-headerpad_max_install_names`; `run_boot.sh` uses
`exec {$ARGV[0]} @ARGV` (paths with spaces) and `SDL_AUDIO_DRIVER=dummy`.

### Divergence A: 8 kernel32 names that the CRT probes through GetProcAddress return 0 (owners: w32-core, w32-sync, w32-mem)

Golden (Wine) returns real addresses for every name the CRT probes:
FlsAlloc, FlsGetValue, FlsSetValue, FlsFree (1 call each), EncodePointer
(11), DecodePointer (23), InitializeCriticalSectionAndSpinCount (1),
IsProcessorFeaturePresent (1). Ours returns 0 for all eight, because
`imp_GetProcAddress` (`runtime/src/win32_core.c:309-335`) only finds names
in the slice export tables, and none of the eight is a game import. The
CRT then takes its no-FLS, no-encode fallback path. Boot survives it,
but the trace diverges from there on.
Proposed change: add the eight to export tables. FlsAlloc/FlsFree/FlsGetValue/
FlsSetValue go on the sync slice's TLS slots; FlsAlloc's callback runs at
thread exit. EncodePointer/DecodePointer can be the identity, because the
CRT only needs them to round-trip. InitializeCriticalSectionAndSpinCount
is InitializeCriticalSection that returns TRUE. IsProcessorFeaturePresent
returns 1 for PF_XMMI_INSTRUCTIONS_AVAILABLE (6) and
PF_XMMI64_INSTRUCTIONS_AVAILABLE (10), else 0. Check: compare.py shows
golden #234-#247 matched with non-zero returns.

### Divergence B: the extra GetModuleHandleA(NULL) at 0xA739BB comes from the Windows version, not the Fls fallback (owner: w32-mem)

The lead's hypothesis was the Fls fallback. The evidence points to the version instead:
- `_encode_pointer` calls 0xA7398D after GetModuleHandleA("KERNEL32.DLL")
  (0xA73A3E).
- 0xA7398D reads `_winmajor` through 0xA6D21B (`[0x10DAFD8]`, set at
  0xA69266 from OSVERSIONINFO+4). If it is > 5, it returns at once
  (0xA739A8 `cmp [ebp-4], 5; jle`). Otherwise it calls GetModuleHandleA(NULL)
  (0xA739B5) and walks the exe's section headers.
- Our GetVersionExA reports Windows XP SP3, 5.1.2600
  (`runtime/src/win32_mem.c:1589-1610`). Wine reports a newer Windows, so the
  golden trace has no such call.
- Fixing divergence A alone does not remove it.

Proposed change: report 6.1.7601 SP1 (Windows 7, the game's era). That
also makes the probes of divergence A consistent with the reported OS,
because XP has no Fls* API. Check: no `00A739BB GetModuleHandleA` line in
the compare output.

### Indirect calls to unknown addresses

These already abort: `RECOMP_ICALL`/`RECOMP_ITAIL` that fail lookup call
`recomp_icall_fail_log` (engine `recomp_glue.c`), which prints the target
and `RECOMP: caller=...` and exits 3. One gap: for an indirect tail jump,
`caller=` reads `[esp]`, which is not a return address (run 8 shows
`caller=0x000000FF`). Owner: engine diag. The guest site (0x96D7E4) had to
be found from the table address instead.

## Runs 10-11 (corrected lift ~/Developer/hp-lift-cfg/generated, ENGINE MacRecomp-hp-int 117fedc)

Scratch engine and scratch lists dropped. Build:
`ENGINE=~/Developer/MacRecomp-hp-int tools/boot/build_boot.sh ~/Developer/hp-lift-cfg/generated`.
`build_boot.sh` compiles any chunk over 4 MB at -O0 (lhp_0082, 0087, 0009).
No chunk has the bts TSO=1 line on this engine.

Core slice (now owned by this lane): GetProcAddress resolves FlsAlloc,
FlsGetValue, FlsSetValue, FlsFree, EncodePointer, DecodePointer,
InitializeCriticalSectionAndSpinCount and IsProcessorFeaturePresent
(`runtime/src/win32_core.c`, export table and the "Functions the CRT finds
only through GetProcAddress" block). FLS values live in host
`_Thread_local` slots, 128 indexes. The FlsAlloc callback is not called;
the CRT's `_freefls` only frees memory. Encode/DecodePointer are the identity.
InitializeCriticalSectionAndSpinCount rewrites the stack and calls the
sync slice's InitializeCriticalSection. Test: `tests/test_win32_core.c`
`test_fls_encode`: `tests/run_all.sh --engine .../MacRecomp-hp-int --only win32_core` PASS.

Run 11: `tools/boot/run_boot.sh 11 HP_TRACE_IMPORTS=1000000` (exit 3).
The trace hit its 1,000,000-call limit. D3DXCompileShader runs.

```
UNSUPPORTED ole32.dll!CoCreateInstance: CLSID {4590F811-1D3A-11D0-891F-00AA004B2E24} IID {DC12A687-737F-11CF-884D-00AA004B2E24} (guest return 0x00941EFB)
warn:  D3D9Cursor::ShowCursor: Not supported on current platform.
UNSUPPORTED d3dx9: D3DXCompileShader flag 0x200
indirect call to unresolved VA 0x00670040
RECOMP: caller=0x0064CDCD esp=0xF07FFD74 icalls=[0x00670040 0x0057BD50 ...]
```

### Blocker: 0x670040 is not a function start (function list, analyze_pe.py)

- The call comes from a guest worker thread (esp 0xF07FFD74, the eighth
  callback stack). 0x64CD80 calls through a handler table, and the target
  0x670040 (`push ecx; mov eax, [0xF54D2C]`, right after int3 padding;
  symbolize: `GUIObject::vfunc_04`) is not lifted.
- Its only reference is an immediate: 0x6A57D8 `push 0x670040`, then
  `call 0x61C4F0` (a handler registration) inside the kept function
  0x6A5790.
- Cause: `find_functions` keeps an immediate code address only when Ghidra
  also listed it (`tools/pe/analyze_pe.py:451` `imm_targets = cands - arms`,
  used at :462-463). Ghidra did not list 0x670040
  (`recon/ghidra_functions.json`, 51,677 starts). The list also has a false
  start at 0x670036 (`mov eax, 1; pop esi; ret`, the tail of the function
  before it).
- Owner: funclist. Proposed fix: accept an `imm` target that is not a Ghidra
  candidate when it starts code after padding or a `ret`, and the immediate
  is pushed or stored as a pointer. Not measured: how many other such
  targets exist.

### Main-thread comparison with the golden trace

`compare.py golden.jsonl run11.log --thread main --ignore ...` matches 864 of
909 events. All CRT start-up and all initializers match. Each divergence,
in order, with the names ignored to get past it:

| # | Golden vs ours | Cause | Owner |
|---|---|---|---|
| 1 | extra `GetModuleHandleA(NULL)` at 0xA739BB | GetVersionExA reports 5.1 (divergence B above) | w32-mem |
| 2 | ours logs FlsGetValue/FlsSetValue, golden does not | Wine forwards them to kernelbase; relay does not log them (trace artefact) | none |
| 3 | golden calls MultiByteToWideChar/SysAllocString/SysFreeString after CoCreateInstance at 0xA2168B | Wine implements the Games Explorer class (gameux), so VerifyAccess runs; ours returns REGDB_E_CLASSNOTREG and the game skips it. Both continue | none (benign) |
| 4 | golden VirtualQuery at 0xA7CC5A after the thread-name RaiseException | real SEH dispatch runs the CRT handler's image check; our RaiseException returns at once for 0x406D1388 | none (benign) |
| 5 | Winsock: ours `gethostbyname` -> 0, so no `inet_ntoa`; ours prints ordinals (`#115` ...) | w32-net returns NULL for the host name; the trace has no names for by-ordinal imports (hp_boot.c, open) | w32-net; boot (trace names) |
| 6 | after `GetFileAttributesA(0xED8494)` -> -1, ours calls `SHCreateDirectoryExA` -> 0x1F (ERROR_GEN_FAILURE); golden opens a file under AppData | The profile folder cannot be created. Golden's prefix already had the folder, but SHCreateDirectoryExA must not fail. `imp_SHCreateDirectoryExA` -> `make_dirs` (`runtime/src/win32_fs.c:1001`). Cause not confirmed: I did not read `make_dirs` or the path string | w32-fs |

Next check for item 6: log the path at 0xED8494 and run make_dirs for it
in `tests/test_win32_fs.c`.

## Divergences A and B implemented (run 12)

A, in the slice that owns each area (replaces the earlier all-in-core
version):
- `runtime/src/win32_sync.c`: FlsAlloc, FlsFree, FlsGetValue, FlsSetValue
  on the TLS slots. FlsGetValue/FlsSetValue are the TLS thunks in the
  table. `tls_alloc`/`tls_free` are shared with TlsAlloc/TlsFree. The
  FlsAlloc callback is stored, and `fls_run_callbacks` calls it with the
  thread's non-NULL value in `thread_main` before `tib_free` (the existing
  thread-exit path). FlsFree does not call it for live threads, and the main
  thread never exits through `thread_main`.
  InitializeCriticalSectionAndSpinCount = `cs_init` + the stored spin count,
  returns TRUE.
- `runtime/src/win32_core.c`: EncodePointer/DecodePointer, the identity
  (the comment says why).
- `runtime/src/win32_mem.c`: IsProcessorFeaturePresent answers from the
  recomp_cpuid profile: TRUE for 2 (CX8), 3 (MMX), 6 (SSE), 8 (RDTSC),
  10 (SSE2); FALSE otherwise (0 FDIV errata, 13 SSE3, ...).

B: `runtime/src/win32_mem.c` `W32_VER_MAJOR/MINOR/BUILD` = 6.2.9200,
platform 2, empty szCSDVersion, service pack 0, used by GetVersionExA and
VerifyVersionInfoA (`k_order`, build check). The game imports no other
version API. No registry CurrentVersion values are served (grep found none).

Tests: `test_win32_sync.c` (table 49 rows; FLS get/set/free; exit callback
called once with the thread's value, not for a thread with no value;
InitializeCriticalSectionAndSpinCount stores 4000),
`test_win32_mem.c` (57 thunks; GetVersionExA 6.2.9200 with no service pack;
VerifyVersionInfoA true for >= 5.1 SP2, >= 6.1 SP1, >= 6.2, false for >= 6.3
and >= 6.2 SP1; IsProcessorFeaturePresent 10/6 TRUE, 13/0 FALSE),
`test_win32_core.c` (Encode/DecodePointer through GetProcAddress, round
trip). Runner: `tests/run_all.sh --engine .../MacRecomp-hp-int --out
.../tmp/run_all/bootab`: 15 passed, 0 failed.

Run 12 (corrected lift, integration engine):
`tools/boot/run_boot.sh 12 HP_TRACE_IMPORTS=1000000`. Same stop as run 11
(0x670040, function list). `compare.py ... --thread main` with no
`--ignore`: 901 of 952 events match (run 9: 308 of 371). The whole CRT
start-up and all initializers now match the golden trace call for call.
The 0xA739BB `GetModuleHandleA(NULL)` is gone. The first divergence is now
the Games Explorer check in WinMain (item 3 of the run 11 table, benign).
The next ones are items 4-6 of that table: VirtualQuery in SEH dispatch
(benign), Winsock `gethostbyname` -> 0 (w32-net), and SHCreateDirectoryExA
-> 0x1F (w32-fs, the first that likely matters).

## Trace: ordinal imports by name; engine note

- Ordinal imports (WS2_32 22, XINPUT1_3 3, OLEAUT32 2) now print by name
  (`WS2_32.dll!WSAStartup` instead of `#115`). The name comes from the
  import inventory: `tools/win32/emit_unsupported_stubs.py` now also
  emits `win32_inventory_name(dll, ordinal)` (the reverse of
  `win32_inventory_ordinal`), and `runtime/src/win32_unsupported.c` was
  regenerated from `notes/win32_imports.md`. Before the change, the
  regenerated file matched the checked-in one byte for byte. Used in
  `resolve_traced` (`runtime/src/hp_boot.c`). Test:
  `test_inventory_name` in `tests/test_win32_core.c`.
- For the engine owner (no engine edit made): an indirect tail jump
  that fails lookup prints a wrong `caller=`. `RECOMP_ITAIL`
  (`MacRecomp-hp-int tools/xrecomp/include/recomp_types.h:1744-1753`)
  calls `recomp_icall_fail_log`, and `recomp_caller_log`
  (`runtime/src/recomp_glue.c:58`) prints `[esp]` as the caller. A tail
  jump pushes no return address, so `[esp]` is whatever the stack holds:
  run 8 printed `caller=0x000000FF` for the `jmp [ecx*4+0x96D828]` at
  0x96D7E4. Proposed: pass the jump site VA to the failure log (the lifter
  knows it, as `RECOMP_ICALL_SAFE_AT` does for calls), and print it as
  `site=`.

## Run 13 (requeued lift with 0x670040, ENGINE MacRecomp-hp-int)

Build: `ENGINE=~/Developer/MacRecomp-hp-int tools/boot/build_boot.sh ~/Developer/hp-lift-cfg/generated`
(lift 04:52, 45,884 functions, 14 unresolved call stubs; analysis
`0c4cd34`/`aeca602`/`cec6a5b`). Run:
`tools/boot/run_boot.sh 13 HP_TRACE_IMPORTS=3000000`.

Result: no crash. The game ran until the 120 s alarm (exit 142). The trace
reached its 3,000,000-call limit. 2,707 PeekMessageA/DispatchMessageA calls
were traced, so **the message pump runs**. The thread names match the
golden run, in the same order: Render, m_readThread, m_decodeThread,
TextureDecode, MusicStream x3, bgprocThread, SaveCore, NuFileStream x4. Not
measured: Present count and frames (the guest COM layer is not traced).

compare.py per thread (`tmp/boot/cmp13.txt`, `cmp13_main.txt`):

| Thread | Golden events | Ours | Matched | First divergence |
|---|---|---|---|---|
| main | 39,558 | 216,482 | 37,558 | WinMain Games Explorer check (benign, run 11 item 3); then items 4-6 of that table |
| m_readThread | 10,405 | 10,404 | 10,404 | none: every one of ours matches |
| Render | 1 | 0 | 0 | trace artefact, see below |

- Render: the golden trace logs calls on entry; ours logs on return. A call
  that never returns (Render's `WaitForSingleObject(event, INFINITE)` at
  0xA3B42B, which is all the golden run shows on that thread) never
  prints. Proposed for golden-trace: print an entry line for calls that
  block (`IMP> n t= name(args) ret RET`), or flush pending calls in the
  fatal or alarm handler.
- New, from the 6.2 version: the GPU-vendor probe 0xA41xxx now does
  `LoadLibraryA("user32.dll")` (0xA41800), then
  `GetProcAddress(user32, "EnumDisplayDevicesA")` (0xA41832), which returns
  0. The game then prints `ERROR: Could Not Find Required procs in
  user32.dll` and goes on. Golden takes its NVIDIA path instead
  (`LoadLibraryA("nvapi.dll")` at 0xA41A76), because Wine's adapter is
  NVIDIA and DXVK reports AMD. EnumDisplayDevicesA is not a game import, so
  the UI slice must export it for GetProcAddress, as for the CRT names
  (owner w32-ui).

## Run 14 (2026-10-24, lift cec6a5b, integration engine, runtime with cb3aca9 + 61fcd80 + d895e72)
- First attempt failed: "d3d device unavailable" MessageBox. Cause: the display was asleep (`system_profiler`: Display Asleep: Yes), so `CGDisplaySwitchToMode` failed for every fullscreen CreateDevice (960x600 x7, then 640x480). Not a code fault. Log kept as run14_asleep.log.
- Rerun under `caffeinate -u -d`: no DXVK errors, ran to the 120 s alarm, trace hit 3M calls. 13 threads in golden order. 2,635 PeekMessageA/DispatchMessageA. One DXVK "Presenter: Actual swapchain properties" line (swapchain built).
- Save folder now exists (made by run 13): GetFileAttributesA returns 0x10 and pcconfig.txt opens, as in golden.
- compare.py: main 38,255 matched (golden 39,558, ours 211,593); m_readThread 10,404 of 10,404; Render 0 of 1 (return-only trace artefact).
- First main divergence unchanged: Games Explorer CoCreateInstance at 0xA2168B (harmless).

## Run 15 (2026-10-24, HP_TRACE_D3D9=1 + HP_TRACE_IMPORTS, EnumDisplayDevicesA exported, 90 s)
- Frames render: 8,460 Present calls, every Present and EndScene hr=0, no Reset, no TestCooperativeLevel change logged.
- Per second: 58 fps at t=9, then 120 fps from t=15 to t=69 with 25k-126k draw calls and 480-964 SetRenderTarget calls per second (real scenes). From t=75: 40-80 fps with exactly 1 draw per frame.
- EnumDisplayDevicesA now answers; the AMD probe moves on to gdi32 and prints "Could Not Find D3DKMTEscape / D3DKMTOpenAdapterFromHdc in gdi32.dll". Not harmless (found 2026-10-25): that failure path returns a non-zero code the caller takes as success, so the uninitialised CrossFire GPU count (the back buffer height, 1290) becomes the GPU count and every scene change renders ~1290 extra frames. Fixed by the D3DKMT exports in win32_ui.c; see notes/scene_change.md.
- Screenshots (shots/run15_{20,45,80}s.png) are all black and identical; a control full-screen capture with no game is black too, so capture shows nothing (display locked/asleep or no Screen Recording permission). Pixel proof needs an in-process back-buffer readback.

## Run 16 (2026-10-24, forced windowed presentation, HP_TRACE_D3D9=1 + HP_TRACE_IMPORTS, 90 s)
- No display mode switch: CreateDevice/Reset/CreateAdditionalSwapChain hand the host Windowed=TRUE, refresh 0; the game's struct keeps Windowed=FALSE/its refresh (d3d9g_windowed_in/out); the SDL window is sized to the 960x600 back buffer (HPRECOMP_BORDERLESS=1 desktop, HPRECOMP_FULLSCREEN=1 old behaviour). DXVK: "Windowed: true", no err lines.
- 13,333 Presents, all hr=0. 57 fps at t=9, 180-204 fps t=15-69 (VerticalSync 0 in pcconfig), 90-150 fps with 1 draw/frame from t=75.
- Screenshots (shots/run16_{20,45,80}s.png, window 960x632 points): 20 s an in-engine intro scene (Harry, Ron, cauldron in a Hogwarts room); 45 s the title screen "LEGO Harry Potter Years 1-4 / Press any key / button to start" over the lake and castle; 80 s attract scene with Hagrid and the Hogwarts Express.
- Visual defects: black hatched streaks on lit surfaces and blocky black patches in the lower right (20 s and 45 s), likely shadow-map depth compare or a shader permutation; 80 s looks clean.

## Runs 17-19 (2026-10-24, in-process frame dump, scripted input)
- HPRECOMP_DUMP_FRAMES=n1,n2,... (+ HPRECOMP_DUMP_DIR) dumps the back buffer before device Present through host calls (GetBackBuffer, CreateOffscreenPlainSurface SYSTEMMEM, GetRenderTargetData, LockRect) to frame_<n>.ppm.
- Run 17 (no input): 300 = TT/LEGO/WB legal and photosensitivity screen; 2000 = intro scene (cat, Ron/Hermione, cauldron); 5000 = black (t=31 s, 205 draws/frame: a fade); 8000 = title screen "Press any key / button to start".
- Run 18 (tools/boot/menu_skip.input, RETURN every 3 s): past the title into the Year 1 opening cutscene in Privet Drive (letterboxed): 5000 = Dumbledore with the Deluminator behind flowers; 7000 = McGonagall in the street; 9000 = Dumbledore and McGonagall; 12000 = Hagrid holding baby Harry. 112 D3DXCompileShader calls, all S_OK; 22 sources match the 286 oracle (front-end) sources, 90 are new (level/cutscene shaders).
- Defect in every 3D frame: large black faceted blotches on characters, hatching on lit surfaces, blocky black rectangles (lower right, sky). Run 19 with DXVK_CONFIG customVendorId 10de (NVIDIA, as golden): same blotches, so the vendor preset path is ruled out.
- Hypothesis (unconfirmed): shadow acne from depth bias on the D24->D32_SFLOAT fallback (DXVK log: "VK_FORMAT_D24_UNORM_S8_UINT -> VK_FORMAT_D32_SFLOAT_S8_UINT"; VK_EXT_depth_bias_control not in KosmicKrisp's enabled list). Alternative: per-triangle vertex data error from lifted code.

## Runs 20-23 (2026-10-24, lift hp-lift-next: 45,882 functions, 0 stubs; render fixes eeceec5/24ae8c4)
- Run 20 (as run 16: 90 s, no input, import + D3D9 trace): 11,162 Presents (run 16: 13,333), all hr=0, no CGDisplaySwitchToMode, no err lines. Steady state 180-200 fps as before; the gap is a cold shader cache (386 D3DXCompileShader calls, the oracle's full count, against 12 in run 16): frame 1000 at 22.6 s (run 16: 15.1 s). compare main 38,564 matched (run 16: 38,230); m_readThread and Render unchanged; first divergences unchanged (Games Explorer, then the LanguageVal registry difference).
- Frames clean: frame 2000 shows Hermione, Harry and Ron at the cauldron with no hatching or black blocks; frame 7000 is the title screen.
- Run 22 (RETURN taps to 45 s only): stops on "Autosave will now use Game 1. OK / Select (Enter)". Run 21: PAD_START/PAD_A/RETURN every 3 s do not skip the opening cutscene.
- Run 23 (tools/boot/level_wait.input: RETURN to 60 s, wait, stick and PAD_A from 240 s; 360 s, BOOT_SECS in run_boot.sh): the opening cutscene ends near 200 s, a load dips to 59 fps at 245 s, then gameplay in the Leaky Cauldron (Year 1) at 150-170 fps with the HUD (Harry and Hagrid portraits, 4 hearts each, studs 0); Harry moves between frames 45000 and 55000. 55,949 Presents, no crash.
- Shaders: run 21 compiled 118 (22 in the 286-source oracle, 96 new), run 23 compiled 172 (26 in the oracle, 146 new); all S_OK.

## Runs 24-26 (2026-10-24, soak and save)
- Run 24 (20 min, tools/boot/soak.input, run-21 save moved aside): no crash, no gap over 1.5 s between per-second summaries; 124,261 Presents; gameplay fps min 38, median 85, max 167; RSS 816 MB at 1 min, peak 858 MB, 651 MB at 19 min (no growth). The scripted pad joined as player 2 (Hagrid, dynamic split screen, 840 studs); the keyboard is player 1. 14 new shaders, 0 failed.
- Save: the autosave dialog wrote SavedGames/Slot1/GAME1.LEGO®_Harry_Potter™SaveGameData (397,364 bytes, created and modified 08:20:07) in the redirected profile; nothing in the Steam folder is newer than the run. No later autosave in the Leaky Cauldron.
- Run 25 (menu map): with a save present the title RETURN shows "Loading game...", then "Autosave will now use Game 1 / OK", then the opening cutscene again (the save is from before it). RETURN does not skip the cutscene.
- Run 26 (330 s, keyboard only, tools/boot/load_kb.input in job tmp): loads Game 1, waits out the cutscene, then keyboard player 1 moves Harry through the Leaky Cauldron (single screen, 10 studs); gameplay fps min 115, median 124, max 176; RSS 635-750 MB; no crash or stall; 14 new shaders.
- Not reached: the Diagon Alley wall (blind stick paths do not navigate the room).

## Runs 27-32 (2026-10-24, key map and the Leaky Cauldron back door)
- Key map in notes/input.md: WASD move, J cast, K switch, RETURN menus; arrows and the other tested keys do nothing visible.
- RETURN taps must run to 60 s: with taps to 36 s, run 28 stopped at "Confirm Load? Yes / No" (the Load Game screen lists Slot 1, Progress 0.0%).
- Frame dump list cap raised from 64 to 1024 entries (d3d9_guest_manual.c DUMP_MAX); run 28's first attempt dumped only the first 64.
- Run 31 (tools/boot/door_walk.input): Harry reaches the back door; the game says "You need a handle to open this door. See if you can find one." (frame 37900; studs 330). Earlier prompt: "Some characters can use magic to lift objects. Press (K) to switch to Hagrid."
- Run 32 (handle_try.input, same timed inputs): a different route; time-based input scripts do not replay a path. No level transition reached.
- No crash in runs 27-32; no new UNSUPPORTED or unhandled-instruction lines (only the Games Explorer CoCreateInstance and D3DXCompileShader flag 0x200); 14 new shaders per run, 0 failed. The only seconds under 90 fps after 235 s are the cutscene-to-gameplay load (3 s at 59 fps).
