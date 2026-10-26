# Boot-path static reachability (full lift in generated.nosync)

Status: done. Static only; nothing was run. Scripts and raw output: job tmp `boot_reach/` (`reach.py` call graph, `analyze.py` ranking and new-list check, `args.py` constant import arguments, `report.txt`, `reach.json`).

## Result

- Reached: **2038 functions** from 299 roots (CRT entry 0xA69393, 6 `__xi`, 284 `__xc`, WndProc 0x9EAF50, threads Render 0xA3B3D0, m_readThread 0x98EA30, m_decodeThread 0x9A6CC0, SoundEvent 0xA37730, TextureDecode **0x984350** (recovered: thread record at 0x9BFDDC holds 0x984350 next to the name string 0xD1B5F0 "TextureDecode"), thread trampoline 0x967410 (CreateThread arg in NuThreadCreate 0x98FD20), atexit 0xA770AB, filter 0xA769C8, EnumDisplayMonitors callback 0x945080). All roots are lifted functions in this lift.
- Edges followed: direct calls, direct tail jumps, jcc exits to other functions, `jmp [IAT]`. Not followed: **862 virtual/register calls**, 5 calls through global pointers, code addresses passed as arguments. So the reach is a lower bound.
- Distance = BFS depth of the function that holds the item, from its nearest root.

| Kind | Reached | Removed by a fix in flight | Left |
|---|---|---|---|
| Unresolved call stubs | 91 | 85 by the CFG list (35 become starts, 50 become in-function) | **6** |
| RECOMP_UNIMPL sites | 127 | 127 by feat/pe-lift-gaps (SSE2 pd/sd, cpuid, pushfd/popfd, repz ret) | **0** |
| Imports | 172 distinct | all 172 bound to a slice thunk; 0 fall to win32_unsupported.c | - |
| Import calls that hit an UNSUPPORTED branch | 8 imports | 0 | **8** |
| Unresolved indirect tail jumps | 24 | 1 by feat/pe-lift-review-fixes; 11 probably by the CFG list (not confirmed, needs a re-lift); 10 are run-time dispatch (7 delay thunks, new-handler `jmp eax` 0xA6CC52, virtual 0x9640F9, `__trandisp1` `jmp [ebx]` 0xA775AB; harmless if the target is lifted) | **2** (0xA30216, 0xA2CB33) (+11 unconfirmed) |

Items not covered by any fix in flight: **16** (6 stubs, 8 import branches, 2 jumps). Add 11 switch tables if the re-lift does not resolve them.

Caveats:
- This lift predates the engine fixes and the CFG list. New-list status is a span check on `<work>/hp-lift-cfg/analysis.json` (target is a start, or lies in the same span as its caller), not a re-lift.
- The `int` UNIMPL sites (18 in the full lift) are not reached.
- "start" means the jump becomes a tail call to a lifted function. That is correct only when the target is a real tail; not checked per site.

## Top findings by distance (all kinds)

| # | d | Item | Where / chain | Owner | Fix in flight |
|---|---|---|---|---|---|
| 1 | 1 | UNIMPL `repz ret` 0xA6E152 | `__security_check_cookie` 0xA6E14A | lifter | yes, pe-lift-gaps 3662e2b (boot_log: hit and fixed) |
| 2 | 1 | UNIMPL pushfd/popfd x4, cpuid x2 (0xA87EDB..0xA87F03) | `__get_sse2_info` 0xA87EC8 (`__xi` init) | lifter | yes, pe-lift-gaps 7eb4f23 |
| 3 | 1 | `jmp eax` 0xA30216 | in 0xA2F650, Render thread | lifter/review | **no**; target not identified |
| 4 | 2 | stub 0xA6936E | `__tmainCRTStartup` > tail jmp into the `__except`/exit block 0xA69354 | function list | **no** (inside 0xA69354 in the new list); exit path only |
| 5 | 2 | RaiseException, code from caller | `_CxxThrowException` 0xA6CC7B @0xA6CCBB (std::bad_alloc path from xc 0xC3CAD0) | w32-core / SEH | **no**: win32_core.c:453 aborts for any code but 0x406D1388 |
| 6 | 2 | GetSystemMetrics(0x57 SM_MEDIACENTER) @0x945178 | WndProc > GameShutdown 0x9D4220 > 0x945170 | w32-ui | **no**: win32_ui.c:1061 default branch |
| 7 | 2 | ShellExecuteA("open", edx) @0x9451C5 | same 0x945170 | w32-fs | **no**: win32_fs.c:1721 always |
| 8 | 2 | UnhandledExceptionFilter @0xA80342 (+0xA6CBC8, 0xA72D37 at d=3) | `__report_gsfailure` 0xA80269, invalid-parameter paths | w32-mem | **no**: win32_mem.c:1778/1781; error paths only |
| 9 | 2 | stubs 0xA74020, 0xA80B0C | filter 0xA769C8 > 0xA73FF2; `___initmbctable` > 0xA8097F | function list | yes, become starts |
| 10 | 2 | UNIMPL pushfd 0xA802BF | `__report_gsfailure` | lifter | yes, pe-lift-gaps |
| 11 | 3 | CoCreateInstance(CLSID_GameExplorer {9A5EA990-3034-4D6F-9128-01F3C61022BC}, IID_IGameExplorer) @0xA21685 | WinMain > CheckGameExplorerAccess 0xA21630 | w32-com | **no**: win32_com.c:72 unregistered CLSID (prints and fails); whether WinMain continues on failure is not checked |
| 12 | 3 | stubs 0xA6A741/42/6A (`_free`), 0xA6E291/A2/A4 (`__calloc_impl`), 0xA75E8A/8D (`__ioinit`), 0xA6D46F | CRT init | function list | yes, become starts |
| 13 | 3 | UNIMPL pushfd/popfd/cpuid x7 | WinMain > DetectCpuFeatures 0x93CB90 | lifter | yes, pe-lift-gaps |
| 14 | 4 | 7 `jmp [.data]` thunks 0xA41AEA.., 0xA4628E | slots 0xED97BC.. hold 0xA41A64 / 0xED9E80 holds 0xA46218 at load; both lifted | - | harmless (run-time dispatch) |
| 15 | 4-6 | 11 bounded switch tables (`ja`, then `jmp [eax*4+0xAA27xx..0xAA4A2E]`) | Render > 0xA389C0 > D3DCreateDevice 0xA37D90 > 0xA21590 > 0xA9684A | function list / lifter | probably: the tables follow the function; the CFG list makes 0xA9684A one span 0xA9684A..0xAA2794 (48 KB). Lifter resolution after the re-lift not confirmed |

Further items not covered, by distance: stubs 0xA6B30A/0xA6B363/0xA6B3CD (d=6, Render > 0xA2F650 > 0x9C2DC0 > 0x9BA8C0 > 0xA6B1D0 > 0xA6B22D: the x87 libm body after `call 0xA777A5` at 0xA6B305; no span in the new list covers 0xA6B30A..0xA6B3F2, so the function returns early); stub 0xA77910 (d=7, `jmp 0xA77910` at 0xA77905 into the middle of 0xA77907); DebugBreak @0x919A5D (d=8, NuInitHardware > 0xA3B930 > 0xA3B5A0 > 0x919A10, win32_mem.c:1789, assert path); socket(AF_INET, SOCK_STREAM, TCP) @0x93C56B and socket(AF_INET, SOCK_DGRAM, UDP) @0x93C585 (d=10, NuInitHardware > FileSystemInit 0x9F3FA0 > NuFileDeviceTM ctor 0x9E4CE0 > 0x9D2F70 > 0x96AEE0 > 0x93C550, win32_net.c:134, disabled by design); stub 0xA7A9E0 (d=12, `_strstr` 0xA6A900 > 0xA7A9F6 jumps back to `lea eax,[edx-1]; pop ebx; ret` before 0xA7A9F0; the stub returns without eax and without `pop ebx`, so every strstr match is wrong); junk decode 0xA2CB33 in D3DCheckShaderModel2 (d=6, still inside span 0xA2C8A0..0xA2CB60; lift_review says not executed).

## Import calls checked against thunk branches

Constant arguments that pass: SystemParametersInfoA 0x10, 0x11, 0x32-0x35, 0x3A, 0x3B (all cases in win32_ui.c); GetLocaleInfoA 0x1004 (in `k_locale`); LCMapStringW 0x100; VirtualAlloc MEM_RESERVE/MEM_COMMIT with PAGE_READWRITE; CoCreateInstance CLSID_AudioReverb_24 @0x95FE03 (registered, win32_xaudio2.c:106); ExtEscape 0x7306 (returns 0, no UNSUPPORTED); CreateDCA "DISPLAY"; RaiseException 0x406D1388 @0x929E7D (SetThreadName, handled); RegQueryValueExA "Hires", "LanguageVal".

Not checked (argument in a register or from the caller): SHGetFolderPathA csidl (4 sites, eax), LCMapStringA/W and GetStringTypeA/W flags from CRT callers, D3DXCompileShader flags, DirectInput8Create version, CreateFileA paths (game files). CreateFileA("CONOUT$") @0xA94FA9 (`___initconout`, shutdown flush) has no special case in win32_fs.c; it becomes a relative path and fails, which the CRT tolerates.

## Stubs reached (91), by distance

| d | Stub | Chain to caller | stub_triage class | New list |
|---|---|---|---|---|
| 2 | 0xA6936E | CRT entry: 0xA69393(WinMainCRTStartup) > 0xA691B3(__tmainCRTStartup) | b_blocked_by_entry | inside 0xA69354 (still stub) |
| 2 | 0xA74020 | unhandled-exception filter: 0xA769C8 > 0xA73FF2 | b_blocked_by_entry | start |
| 2 | 0xA80B0C | xi init: 0xA80B19(___initmbctable) > 0xA8097F | b_blocked_by_entry | start |
| 3 | 0xA6A741 | xi init: 0xA80B19(___initmbctable) > 0xA8097F > 0xA6A6E2(_free) | b_blocked_by_entry | start |
| 3 | 0xA6A742 | xi init: 0xA80B19(___initmbctable) > 0xA8097F > 0xA6A6E2(_free) | b_blocked_by_entry | start |
| 3 | 0xA6A76A | xi init: 0xA80B19(___initmbctable) > 0xA8097F > 0xA6A6E2(_free) | b_blocked_by_entry | start |
| 3 | 0xA6D46F | WndProc: 0x9EAF50(TTalesWindow_WndProc) > 0xA6D475 > 0xA6D393 | b_blocked_by_entry | start |
| 3 | 0xA6E291 | xi init: 0xA68198(___onexitinit) > 0xA719F6(__calloc_crt) > 0xA6E18C(__calloc_impl) | b_blocked_by_entry | start |
| 3 | 0xA6E2A2 | xi init: 0xA68198(___onexitinit) > 0xA719F6(__calloc_crt) > 0xA6E18C(__calloc_impl) | b_blocked_by_entry | start |
| 3 | 0xA6E2A4 | xi init: 0xA68198(___onexitinit) > 0xA719F6(__calloc_crt) > 0xA6E18C(__calloc_impl) | b_blocked_by_entry | start |
| 3 | 0xA75E8A | CRT entry: 0xA69393(WinMainCRTStartup) > 0xA691B3(__tmainCRTStartup) > 0xA75C3F(__ioinit) | b_blocked_by_entry | start |
| 3 | 0xA75E8D | CRT entry: 0xA69393(WinMainCRTStartup) > 0xA691B3(__tmainCRTStartup) > 0xA75C3F(__ioinit) | b_blocked_by_entry | start |
| 4 | 0x93CED0 | CRT entry: 0xA69393(WinMainCRTStartup) > 0xA691B3(__tmainCRTStartup) > 0xA217A0(WinMain) > 0x93CB90(DetectCpuFeatures) | b_blocked_by_entry | start |
| 4 | 0xA68605 | xc init: 0xC3BDF0 > 0xA690B7(`eh vector ctor iterator') > 0xA69104 > 0xA685B6(__ArrayUnwind) | b_blocked_by_entry | start |
| 4 | 0xA75886 | CRT entry: 0xA69393(WinMainCRTStartup) > 0xA691B3(__tmainCRTStartup) > 0xA6D0C3(__amsg_exit) > 0xA75778 | b_fixed_by_drop_merge | internal |
| 4 | 0xA758DF | CRT entry: 0xA69393(WinMainCRTStartup) > 0xA691B3(__tmainCRTStartup) > 0xA6D0C3(__amsg_exit) > 0xA75778 | b_fixed_by_drop_merge | internal |
| 4 | 0xA75912 | CRT entry: 0xA69393(WinMainCRTStartup) > 0xA691B3(__tmainCRTStartup) > 0xA6D0C3(__amsg_exit) > 0xA75778 | b_fixed_by_drop_merge | internal |
| 4 | 0xA75913 | CRT entry: 0xA69393(WinMainCRTStartup) > 0xA691B3(__tmainCRTStartup) > 0xA6D0C3(__amsg_exit) > 0xA75778 | b_fixed_by_drop_merge | internal |
| 4 | 0xA7FF80 | CRT entry: 0xA69393(WinMainCRTStartup) > 0xA691B3(__tmainCRTStartup) > 0xA6D301(__cinit) > 0xA7FEE0(__IsNonwritableInCurrentImage) | b_blocked_by_entry | start |
| 5 | 0xA6DBF2 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21440 > 0xA6DA6F(_raise) | b_blocked_by_entry | start |
| 5 | 0xA6DBF6 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21440 > 0xA6DA6F(_raise) | b_blocked_by_entry | start |
| 5 | 0xA6DC19 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21440 > 0xA6DA6F(_raise) | b_blocked_by_entry | start |
| 5 | 0xA79ADB | xi init: 0xA80B19(___initmbctable) > 0xA8097F > 0xA6A6E2(_free) > 0xA79AC9 > 0xA79AED | b_merge_short_of_target | internal |
| 5 | 0xA7ADEC | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA2F650 > 0xA1C310 > 0x9F0B80 > 0xA6AD40 | b_blocked_by_entry | start |
| 5 | 0xA9A380 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A | b_blocked_by_entry | internal |
| 6 | 0x9E6BC6 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0x9F8AD0 > 0x9F05E0 > 0x9E6450 | b_fixed_by_drop_merge | internal |
| 6 | 0xA28A81 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA36AE0 > 0xA32310 > 0xA288A0 | b_fixed_by_drop_merge | internal |
| 6 | 0xA28B5C | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA36AE0 > 0xA32310 > 0xA288A0 | b_fixed_by_drop_merge | internal |
| 6 | 0xA6B30A | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA2F650 > 0x9C2DC0 > 0x9BA8C0 > 0xA6B1D0 > 0xA6B22D | b_fixed_by_drop_merge | not covered |
| 6 | 0xA6B363 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA2F650 > 0x9C2DC0 > 0x9BA8C0 > 0xA6B1D0 > 0xA6B22D | b_fixed_by_drop_merge | not covered |
| 6 | 0xA6B3CD | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA2F650 > 0x9C2DC0 > 0x9BA8C0 > 0xA6B1D0 > 0xA6B22D | b_fixed_by_drop_merge | not covered |
| 6 | 0xA6DF51 | xc init: 0xC3EAA8 > 0xA68203(_atexit) > 0xA681C7(__onexit) > 0xA680DF > 0xA71A3E(__realloc_crt) > 0xA6DE04(_realloc) | b_blocked_by_entry | start |
| 6 | 0xA6DF54 | xc init: 0xC3EAA8 > 0xA68203(_atexit) > 0xA681C7(__onexit) > 0xA680DF > 0xA71A3E(__realloc_crt) > 0xA6DE04(_realloc) | b_blocked_by_entry | start |
| 6 | 0xA6DFD8 | xc init: 0xC3EAA8 > 0xA68203(_atexit) > 0xA681C7(__onexit) > 0xA680DF > 0xA71A3E(__realloc_crt) > 0xA6DE04(_realloc) | b_blocked_by_entry | start |
| 6 | 0xA6DFDD | xc init: 0xC3EAA8 > 0xA68203(_atexit) > 0xA681C7(__onexit) > 0xA680DF > 0xA71A3E(__realloc_crt) > 0xA6DE04(_realloc) | b_blocked_by_entry | start |
| 6 | 0xA6DFEF | xc init: 0xC3EAA8 > 0xA68203(_atexit) > 0xA681C7(__onexit) > 0xA680DF > 0xA71A3E(__realloc_crt) > 0xA6DE04(_realloc) | b_blocked_by_entry | start |
| 6 | 0xA6DFF1 | xc init: 0xC3EAA8 > 0xA68203(_atexit) > 0xA681C7(__onexit) > 0xA680DF > 0xA71A3E(__realloc_crt) > 0xA6DE04(_realloc) | b_blocked_by_entry | start |
| 6 | 0xA7B098 | xc init: 0xC3C200 > 0x684530(PhysicsEffects::ctor_or_dtor) > 0x936070 > 0xA6AE9C > 0xA7AEC0 > 0xA7AEDE | b_blocked_by_entry | internal |
| 6 | 0xA7B09F | xc init: 0xC3C200 > 0x684530(PhysicsEffects::ctor_or_dtor) > 0x936070 > 0xA6AE9C > 0xA7AEC0 > 0xA7AEDE | b_blocked_by_entry | internal |
| 6 | 0xA7B0D2 | xc init: 0xC3C200 > 0x684530(PhysicsEffects::ctor_or_dtor) > 0x936070 > 0xA6AE9C > 0xA7AEC0 > 0xA7AEDE | b_blocked_by_entry | internal |
| 6 | 0xA7B14E | xc init: 0xC3C200 > 0x684530(PhysicsEffects::ctor_or_dtor) > 0x936070 > 0xA6AE9C > 0xA7AEC0 > 0xA7AEDE | b_blocked_by_entry | internal |
| 6 | 0xA96C94 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA96B2D | b_fixed_by_drop_merge | internal |
| 6 | 0xA96FC8 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA96DDE | b_fixed_by_drop_merge | internal |
| 6 | 0xA977B6 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA9720B | b_fixed_by_drop_merge | internal |
| 6 | 0xA98043 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA97B30 | b_fixed_by_drop_merge | internal |
| 6 | 0xA98B77 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA98A4B | b_blocked_by_entry | internal |
| 6 | 0xA9915E | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA99024 | b_fixed_by_drop_merge | internal |
| 6 | 0xA991F8 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA99024 | b_fixed_by_drop_merge | internal |
| 6 | 0xA99214 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA99024 | b_fixed_by_drop_merge | internal |
| 6 | 0xA99363 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA99024 | b_fixed_by_drop_merge | internal |
| 6 | 0xA9963A | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA99024 | b_fixed_by_drop_merge | internal |
| 6 | 0xA9F31A | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA99024 | b_blocked_by_entry | internal |
| 6 | 0xA9F324 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA99024 | b_blocked_by_entry | internal |
| 6 | 0xA9F32E | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA99024 | b_blocked_by_entry | internal |
| 6 | 0xA9F342 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA99024 | b_blocked_by_entry | internal |
| 6 | 0xA9F38B | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA99024 | b_blocked_by_entry | internal |
| 6 | 0xA9F395 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA99024 | b_blocked_by_entry | internal |
| 6 | 0xA9F39F | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA99024 | b_blocked_by_entry | internal |
| 6 | 0xA9F3A9 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA99024 | b_blocked_by_entry | internal |
| 6 | 0xA9F3E5 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA99024 | b_blocked_by_entry | internal |
| 6 | 0xA9F47C | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA99024 | b_blocked_by_entry | internal |
| 6 | 0xA9F58F | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > 0xA37D90(D3DCreateDevice) > 0xA21590 > 0xA9684A > 0xA99024 | b_blocked_by_entry | internal |
| 7 | 0x5D3D74 | xc init: 0xC3BCC0 > 0x40366B > .. > 0x67EAD0 > 0x633FD0 > 0x5D3D20 | b_fixed_by_drop_merge | internal |
| 7 | 0x5D3D96 | xc init: 0xC3BCC0 > 0x40366B > .. > 0x67EAD0 > 0x633FD0 > 0x5D3D20 | b_fixed_by_drop_merge | internal |
| 7 | 0x5D3DA0 | xc init: 0xC3BCC0 > 0x40366B > .. > 0x67EAD0 > 0x633FD0 > 0x5D3D20 | b_fixed_by_drop_merge | internal |
| 7 | 0x9D4734 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA2F650 > .. > 0x9F7690 > 0x9EB5E0 > 0x9D45B0 | b_blocked_by_entry | start |
| 7 | 0x9D47E6 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA2F650 > .. > 0x9F7690 > 0x9EB5E0 > 0x9D45B0 | b_blocked_by_entry | start |
| 7 | 0x9D4984 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA2F650 > .. > 0x9F7720 > 0x9EB670 > 0x9D4800 | b_blocked_by_entry | start |
| 7 | 0x9D4A36 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA2F650 > .. > 0x9F7720 > 0x9EB670 > 0x9D4800 | b_blocked_by_entry | start |
| 7 | 0xA77910 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA2F650 > .. > 0xA6B1D0 > 0xA6B22D > 0xA778F0 | b_blocked_by_entry | inside 0xA77907 (still stub) |
| 8 | 0x980DD4 | CRT entry: 0xA69393(WinMainCRTStartup) > 0xA691B3(__tmainCRTStartup) > .. > 0xA3CA00(NuInitHardware) > 0xA36270 > 0x980D60 | b_fixed_by_drop_merge | internal |
| 8 | 0x98115E | CRT entry: 0xA69393(WinMainCRTStartup) > 0xA691B3(__tmainCRTStartup) > .. > 0xA3CA00(NuInitHardware) > 0xA36270 > 0x980D60 | b_fixed_by_drop_merge | internal |
| 8 | 0x9812B7 | CRT entry: 0xA69393(WinMainCRTStartup) > 0xA691B3(__tmainCRTStartup) > .. > 0xA3CA00(NuInitHardware) > 0xA36270 > 0x980D60 | b_fixed_by_drop_merge | internal |
| 10 | 0x9526F6 | CRT entry: 0xA69393(WinMainCRTStartup) > 0xA691B3(__tmainCRTStartup) > .. > 0xA35FE0 > 0xA2E050 > 0x9526C0 | b_fixed_by_drop_merge | internal |
| 10 | 0x952750 | CRT entry: 0xA69393(WinMainCRTStartup) > 0xA691B3(__tmainCRTStartup) > .. > 0xA35FE0 > 0xA2E050 > 0x9526C0 | b_fixed_by_drop_merge | internal |
| 10 | 0x952757 | CRT entry: 0xA69393(WinMainCRTStartup) > 0xA691B3(__tmainCRTStartup) > .. > 0xA35FE0 > 0xA2E050 > 0x9526C0 | b_fixed_by_drop_merge | internal |
| 10 | 0xA72C27 | xc init: 0xC3EADD > 0xA66FFD(std::basic_ostream<char, std::char_traits<char>>::basic_ostream<char, std::char_traits<char>>) > .. > 0xA662C3(std::_Locinfo::~_Locinfo) > 0xA675B5(std::_Locinfo::_Locinfo_dtor) > 0xA72AE2 | b_blocked_by_entry | start |
| 10 | 0xA72C44 | xc init: 0xC3EADD > 0xA66FFD(std::basic_ostream<char, std::char_traits<char>>::basic_ostream<char, std::char_traits<char>>) > .. > 0xA662C3(std::_Locinfo::~_Locinfo) > 0xA675B5(std::_Locinfo::_Locinfo_dtor) > 0xA72AE2 | b_blocked_by_entry | start |
| 10 | 0xA8119C | WndProc: 0x9EAF50(TTalesWindow_WndProc) > 0x9D4220(GameShutdown) > .. > 0xA8FF2A(__sopen_helper) > 0xA8F71B > 0xA810C0(__alloc_osfhnd) | b_blocked_by_entry | start |
| 10 | 0xA811BD | WndProc: 0x9EAF50(TTalesWindow_WndProc) > 0x9D4220(GameShutdown) > .. > 0xA8FF2A(__sopen_helper) > 0xA8F71B > 0xA810C0(__alloc_osfhnd) | b_blocked_by_entry | start |
| 10 | 0xA811C9 | WndProc: 0x9EAF50(TTalesWindow_WndProc) > 0x9D4220(GameShutdown) > .. > 0xA8FF2A(__sopen_helper) > 0xA8F71B > 0xA810C0(__alloc_osfhnd) | b_blocked_by_entry | start |
| 10 | 0xA8123C | WndProc: 0x9EAF50(TTalesWindow_WndProc) > 0x9D4220(GameShutdown) > .. > 0xA8FF2A(__sopen_helper) > 0xA8F71B > 0xA810C0(__alloc_osfhnd) | b_blocked_by_entry | start |
| 10 | 0xA8124B | WndProc: 0x9EAF50(TTalesWindow_WndProc) > 0x9D4220(GameShutdown) > .. > 0xA8FF2A(__sopen_helper) > 0xA8F71B > 0xA810C0(__alloc_osfhnd) | b_blocked_by_entry | start |
| 12 | 0xA724C1 | xc init: 0xC3EADD > 0xA66FFD(std::basic_ostream<char, std::char_traits<char>>::basic_ostream<char, std::char_traits<char>>) > .. > 0xA72AE2 > 0xA7281C > 0xA72352 | b_fixed_by_drop_merge | internal |
| 12 | 0xA72512 | xc init: 0xC3EADD > 0xA66FFD(std::basic_ostream<char, std::char_traits<char>>::basic_ostream<char, std::char_traits<char>>) > .. > 0xA72AE2 > 0xA7281C > 0xA72352 | b_fixed_by_drop_merge | internal |
| 12 | 0xA72514 | xc init: 0xC3EADD > 0xA66FFD(std::basic_ostream<char, std::char_traits<char>>::basic_ostream<char, std::char_traits<char>>) > .. > 0xA72AE2 > 0xA7281C > 0xA72352 | b_fixed_by_drop_merge | internal |
| 12 | 0xA7A9E0 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > .. > 0x9F6100 > 0xA6A900(_strstr) > 0xA7A9F6 | b_backward_owner_not_found | not covered |
| 13 | 0xA84B51 | xc init: 0xC3EADD > 0xA66FFD(std::basic_ostream<char, std::char_traits<char>>::basic_ostream<char, std::char_traits<char>>) > .. > 0xA7281C > 0xA72352 > 0xA849B3 | b_fixed_by_drop_merge | internal |
| 13 | 0xA84B62 | xc init: 0xC3EADD > 0xA66FFD(std::basic_ostream<char, std::char_traits<char>>::basic_ostream<char, std::char_traits<char>>) > .. > 0xA7281C > 0xA72352 > 0xA849B3 | b_fixed_by_drop_merge | internal |
| 13 | 0xA84B8A | xc init: 0xC3EADD > 0xA66FFD(std::basic_ostream<char, std::char_traits<char>>::basic_ostream<char, std::char_traits<char>>) > .. > 0xA7281C > 0xA72352 > 0xA849B3 | b_fixed_by_drop_merge | internal |
| 13 | 0xA84B8F | xc init: 0xC3EADD > 0xA66FFD(std::basic_ostream<char, std::char_traits<char>>::basic_ostream<char, std::char_traits<char>>) > .. > 0xA7281C > 0xA72352 > 0xA849B3 | b_fixed_by_drop_merge | internal |

## UNIMPL sites reached (127), by distance

| d | Site | Instruction | Function | Fix |
|---|---|---|---|---|
| 1 | 0xA6E152 | `repz ret` | 0xA6E14A(@__security_check_cookie@4) | pe-lift-gaps |
| 1 | 0xA87EDB | `pushfd` | 0xA87EC8(__get_sse2_info) | pe-lift-gaps |
| 1 | 0xA87EE5 | `popfd` | 0xA87EC8(__get_sse2_info) | pe-lift-gaps |
| 1 | 0xA87EE6 | `pushfd` | 0xA87EC8(__get_sse2_info) | pe-lift-gaps |
| 1 | 0xA87EED | `popfd` | 0xA87EC8(__get_sse2_info) | pe-lift-gaps |
| 1 | 0xA87EF0 | `cpuid` | 0xA87EC8(__get_sse2_info) | pe-lift-gaps |
| 1 | 0xA87F03 | `cpuid` | 0xA87EC8(__get_sse2_info) | pe-lift-gaps |
| 2 | 0xA802BF | `pushfd` | 0xA80269(___report_gsfailure) | pe-lift-gaps |
| 3 | 0x93CBB6 | `pushfd` | 0x93CB90(DetectCpuFeatures) | pe-lift-gaps |
| 3 | 0x93CBC0 | `popfd` | 0x93CB90(DetectCpuFeatures) | pe-lift-gaps |
| 3 | 0x93CBC1 | `pushfd` | 0x93CB90(DetectCpuFeatures) | pe-lift-gaps |
| 3 | 0x93CC13 | `cpuid` | 0x93CB90(DetectCpuFeatures) | pe-lift-gaps |
| 3 | 0x93CCB0 | `cpuid` | 0x93CB90(DetectCpuFeatures) | pe-lift-gaps |
| 3 | 0x93CE43 | `cpuid` | 0x93CB90(DetectCpuFeatures) | pe-lift-gaps |
| 3 | 0x93CE53 | `cpuid` | 0x93CB90(DetectCpuFeatures) | pe-lift-gaps |
| 3 | 0x93CE73 | `cpuid` | 0x93CB90(DetectCpuFeatures) | pe-lift-gaps |
| 3 | 0x93CE8E | `cpuid` | 0x93CB90(DetectCpuFeatures) | pe-lift-gaps |
| 3 | 0xA6CB5F | `pushfd` | 0xA6CB0A(__invoke_watson) | pe-lift-gaps |
| 4 | 0xA6AD9F | `andpd xmm0, xmmword ptr [0xd1fc60]` | 0xA6AD40 | pe-lift-gaps |
| 4 | 0xA6AE10 | `cmpnlepd xmm0, xmm1` | 0xA6AD40 | pe-lift-gaps |
| 4 | 0xA6AE23 | `andpd xmm0, xmmword ptr [0xd1fc30]` | 0xA6AD40 | pe-lift-gaps |
| 4 | 0xA6AE41 | `cmpnlepd xmm3, xmmword ptr [0xd1fc50]` | 0xA6AD40 | pe-lift-gaps |
| 4 | 0xA6AE4A | `andpd xmm3, xmmword ptr [0xd1fc30]` | 0xA6AD40 | pe-lift-gaps |
| 5 | 0xA7AEDE | `unpcklpd xmm0, xmm0` | 0xA7AEDE | pe-lift-gaps |
| 5 | 0xA7AF26 | `mulpd xmm1, xmm0` | 0xA7AEDE | pe-lift-gaps |
| 5 | 0xA7AF32 | `subpd xmm1, xmm6` | 0xA7AEDE | pe-lift-gaps |
| 5 | 0xA7AF36 | `mulpd xmm2, xmm1` | 0xA7AEDE | pe-lift-gaps |
| 5 | 0xA7AF42 | `mulpd xmm3, xmm1` | 0xA7AEDE | pe-lift-gaps |
| 5 | 0xA7AF4E | `subpd xmm0, xmm2` | 0xA7AEDE | pe-lift-gaps |
| 5 | 0xA7AF63 | `subpd xmm0, xmm3` | 0xA7AEDE | pe-lift-gaps |
| 5 | 0xA7AF6F | `mulpd xmm4, xmm0` | 0xA7AEDE | pe-lift-gaps |
| 5 | 0xA7AF77 | `mulpd xmm0, xmm0` | 0xA7AEDE | pe-lift-gaps |
| 5 | 0xA7AF87 | `unpckhpd xmm2, xmm2` | 0xA7AEDE | pe-lift-gaps |
| 5 | 0xA7AFA8 | `mulpd xmm0, xmm5` | 0xA7AEDE | pe-lift-gaps |
| 5 | 0xA7AFB0 | `orpd xmm2, xmm7` | 0xA7AEDE | pe-lift-gaps |
| 5 | 0xA7AFB4 | `unpckhpd xmm0, xmm0` | 0xA7AEDE | pe-lift-gaps |
| 5 | 0xA7B01C | `orpd xmm6, xmm3` | 0xA7AEDE | pe-lift-gaps |
| 5 | 0xA7B637 | `unpcklpd xmm0, xmm0` | 0xA7B62E | pe-lift-gaps |
| 5 | 0xA7B665 | `andpd xmm0, xmm1` | 0xA7B62E | pe-lift-gaps |
| 5 | 0xA7B669 | `orpd xmm0, xmm3` | 0xA7B62E | pe-lift-gaps |
| 5 | 0xA7B68B | `andpd xmm6, xmm0` | 0xA7B62E | pe-lift-gaps |
| 5 | 0xA7B68F | `subpd xmm0, xmm6` | 0xA7B62E | pe-lift-gaps |
| 5 | 0xA7B693 | `mulpd xmm6, xmm4` | 0xA7B62E | pe-lift-gaps |
| 5 | 0xA7B697 | `subpd xmm6, xmm3` | 0xA7B62E | pe-lift-gaps |
| 5 | 0xA7B69F | `mulpd xmm0, xmm4` | 0xA7B62E | pe-lift-gaps |
| 5 | 0xA7B6CC | `unpcklpd xmm6, xmm6` | 0xA7B62E | pe-lift-gaps |
| 5 | 0xA7B6F9 | `mulpd xmm1, xmm0` | 0xA7B62E | pe-lift-gaps |
| 5 | 0xA7B6FD | `mulpd xmm3, xmm3` | 0xA7B62E | pe-lift-gaps |
| 5 | 0xA7B719 | `mulpd xmm6, xmm5` | 0xA7B62E | pe-lift-gaps |
| 5 | 0xA7B725 | `andpd xmm4, xmm5` | 0xA7B62E | pe-lift-gaps |
| 5 | 0xA7B731 | `mulpd xmm1, xmm0` | 0xA7B62E | pe-lift-gaps |
| 5 | 0xA7B741 | `unpckhpd xmm6, xmm6` | 0xA7B62E | pe-lift-gaps |
| 5 | 0xA7B745 | `mulpd xmm1, xmm3` | 0xA7B62E | pe-lift-gaps |
| 5 | 0xA7B750 | `unpckhpd xmm1, xmm1` | 0xA7B62E | pe-lift-gaps |
| 5 | 0xA7B77C | `cmpeqsd xmm1, xmm0` | 0xA7B62E | pe-lift-gaps |
| 5 | 0xA7B7AE | `andpd xmm0, xmm1` | 0xA7B62E | pe-lift-gaps |
| 5 | 0xA7B7B2 | `orpd xmm0, xmm2` | 0xA7B62E | pe-lift-gaps |
| 5 | 0xA7B7B6 | `cmpeqsd xmm2, xmm0` | 0xA7B62E | pe-lift-gaps |
| 6 | 0x96F7C3 | `cpuid` | 0x96F6D0(InitProfilePath) | pe-lift-gaps |
| 6 | 0xA7B8A7 | `unpcklpd xmm0, xmm0` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7B8DD | `andpd xmm0, xmm1` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7B8E1 | `orpd xmm0, xmm3` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7B903 | `andpd xmm6, xmm0` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7B907 | `subpd xmm0, xmm6` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7B90B | `mulpd xmm6, xmm4` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7B90F | `subpd xmm6, xmm2` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7B917 | `mulpd xmm0, xmm4` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7B944 | `unpcklpd xmm6, xmm6` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7B971 | `mulpd xmm1, xmm0` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7B975 | `mulpd xmm3, xmm3` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7B991 | `mulpd xmm6, xmm5` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7B99D | `andpd xmm4, xmm5` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7B9A9 | `mulpd xmm1, xmm0` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7B9BD | `mulpd xmm2, xmm0` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7B9C5 | `unpckhpd xmm6, xmm6` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7B9C9 | `mulpd xmm1, xmm3` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7B9D8 | `unpckhpd xmm0, xmm0` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7BA04 | `cmpeqsd xmm1, xmm0` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7BA36 | `andpd xmm0, xmm1` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7BA3A | `orpd xmm0, xmm2` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7BA3E | `cmpeqsd xmm2, xmm0` | 0xA7B89E | pe-lift-gaps |
| 6 | 0xA7BB3F | `andpd xmm7, xmm0` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BB51 | `orpd xmm7, xmm2` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BBCD | `andpd xmm4, xmmword ptr [0xd25fd0]` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BBFF | `orpd xmm4, xmmword ptr [0xd25fe0]` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BC0B | `andpd xmm1, xmm4` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BC59 | `andpd xmm0, xmm5` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BCBD | `unpcklpd xmm7, xmm7` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BD08 | `andpd xmm3, xmm1` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BD0C | `andpd xmm4, xmm6` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BD9D | `mulpd xmm2, xmm7` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BDA9 | `mulpd xmm7, xmm7` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BDC9 | `mulpd xmm1, xmm7` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BE1C | `mulpd xmm3, xmm1` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BE24 | `unpcklpd xmm0, xmm0` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BE28 | `mulpd xmm6, xmm0` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BE30 | `mulpd xmm0, xmm0` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BE38 | `mulpd xmm7, xmm0` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BE8D | `andpd xmm1, xmm3` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BEC7 | `orpd xmm2, xmm3` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BEE1 | `pmaxsw xmm1, xmm3` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BEED | `pmovmskb eax, xmm2` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BF2A | `andpd xmm1, xmmword ptr [0xd25ff0]` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BF4B | `pmovmskb eax, xmm2` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7BFF8 | `andpd xmm7, xmm0` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C000 | `andpd xmm0, xmmword ptr [0xd25ff0]` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C012 | `orpd xmm7, xmm2` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C0B4 | `andpd xmm3, xmm4` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C0BC | `pmovmskb ecx, xmm1` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C107 | `andpd xmm1, xmmword ptr [0xd25ff0]` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C124 | `pmovmskb eax, xmm2` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C200 | `andpd xmm3, xmm2` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C208 | `pmovmskb eax, xmm1` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C251 | `andpd xmm3, xmm2` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C259 | `pmovmskb eax, xmm1` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C3CB | `mulpd xmm2, xmm7` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C3D7 | `mulpd xmm7, xmm7` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C3F7 | `mulpd xmm1, xmm7` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C45F | `mulpd xmm3, xmm1` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C467 | `unpcklpd xmm0, xmm0` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C46B | `mulpd xmm6, xmm0` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C473 | `mulpd xmm0, xmm0` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C47B | `mulpd xmm7, xmm0` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C4B6 | `andpd xmm1, xmm3` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C4D6 | `andpd xmm1, xmm4` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C59E | `orpd xmm0, xmm1` | 0xA7BB29 | pe-lift-gaps |
| 6 | 0xA7C5E1 | `mulpd xmm4, xmm6` | 0xA7BB29 | pe-lift-gaps |

## Imports reached (172), by distance

| d | Import | First caller | Thunk |
|---|---|---|---|
| 0 | KERNEL32.dll!LeaveCriticalSection | 0xA3B3D0(RenderThreadProc) | win32_sync.c:imp_LeaveCriticalSection |
| 0 | KERNEL32.dll!QueryPerformanceCounter | 0x984350 | win32_sync.c:imp_QueryPerformanceCounter |
| 0 | KERNEL32.dll!SetUnhandledExceptionFilter | 0xA76A05 | win32_mem.c:imp_SetUnhandledExceptionFilter |
| 0 | KERNEL32.dll!Sleep | 0xA37730 | win32_sync.c:imp_Sleep |
| 0 | KERNEL32.dll!TlsGetValue | 0xA3B3D0(RenderThreadProc) | win32_sync.c:imp_TlsGetValue |
| 0 | KERNEL32.dll!WaitForSingleObject | 0xA3B3D0(RenderThreadProc) | win32_sync.c:imp_WaitForSingleObject |
| 0 | USER32.dll!DefWindowProcA | 0x9EAF50(TTalesWindow_WndProc) | win32_ui.c:imp_DefWindowProcA |
| 0 | USER32.dll!PostQuitMessage | 0x9EAF50(TTalesWindow_WndProc) | win32_ui.c:imp_PostQuitMessage |
| 0 | USER32.dll!SetCursor | 0x9EAF50(TTalesWindow_WndProc) | win32_ui.c:imp_SetCursor |
| 1 | ADVAPI32.dll!RegCloseKey | 0x943B60(ReadRegistryHires) | win32_fs.c:imp_RegCloseKey |
| 1 | ADVAPI32.dll!RegOpenKeyExA | 0x943B60(ReadRegistryHires) | win32_fs.c:imp_RegOpenKeyExA |
| 1 | ADVAPI32.dll!RegQueryValueExA | 0x943B60(ReadRegistryHires) | win32_fs.c:imp_RegQueryValueExA |
| 1 | KERNEL32.dll!CreateSemaphoreA | 0x971EF0 | win32_sync.c:imp_CreateSemaphoreA |
| 1 | KERNEL32.dll!EnterCriticalSection | 0xA33260(NuSoundFileRequestWAV::ctor_or_dtor) | win32_sync.c:imp_EnterCriticalSection |
| 1 | KERNEL32.dll!GetCommandLineA | 0xA691B3(__tmainCRTStartup) | win32_mem.c:imp_GetCommandLineA |
| 1 | KERNEL32.dll!GetCurrentProcessId | 0xA771FE(___security_init_cookie) | win32_mem.c:imp_GetCurrentProcessId |
| 1 | KERNEL32.dll!GetCurrentThreadId | 0xA771FE(___security_init_cookie) | win32_sync.c:imp_GetCurrentThreadId |
| 1 | KERNEL32.dll!GetModuleHandleA | 0xA739F9 | win32_core.c:imp_GetModuleHandleA |
| 1 | KERNEL32.dll!GetProcAddress | 0xA739F9 | win32_core.c:imp_GetProcAddress |
| 1 | KERNEL32.dll!GetProcessHeap | 0xA691B3(__tmainCRTStartup) | win32_mem.c:imp_GetProcessHeap |
| 1 | KERNEL32.dll!GetStartupInfoA | 0xA691B3(__tmainCRTStartup) | win32_mem.c:imp_GetStartupInfoA |
| 1 | KERNEL32.dll!GetSystemTimeAsFileTime | 0xA771FE(___security_init_cookie) | win32_sync.c:imp_GetSystemTimeAsFileTime |
| 1 | KERNEL32.dll!GetTickCount | 0xA771FE(___security_init_cookie) | win32_sync.c:imp_GetTickCount |
| 1 | KERNEL32.dll!GetVersionExA | 0xA691B3(__tmainCRTStartup) | win32_mem.c:imp_GetVersionExA |
| 1 | KERNEL32.dll!HeapAlloc | 0xA691B3(__tmainCRTStartup) | win32_mem.c:imp_HeapAlloc |
| 1 | KERNEL32.dll!HeapFree | 0xA691B3(__tmainCRTStartup) | win32_mem.c:imp_HeapFree |
| 1 | KERNEL32.dll!InitializeCriticalSection | 0x9A5580 | win32_sync.c:imp_InitializeCriticalSection |
| 1 | KERNEL32.dll!InterlockedDecrement | 0xA8097F | win32_sync.c:imp_InterlockedDecrement |
| 1 | KERNEL32.dll!InterlockedIncrement | 0xA8097F | win32_sync.c:imp_InterlockedIncrement |
| 1 | KERNEL32.dll!ReleaseSemaphore | 0x971EF0 | win32_sync.c:imp_ReleaseSemaphore |
| 1 | KERNEL32.dll!SetThreadExecutionState | 0x9D4220(GameShutdown) | win32_sync.c:imp_SetThreadExecutionState |
| 1 | USER32.dll!DestroyWindow | 0x9D4220(GameShutdown) | win32_ui.c:imp_DestroyWindow |
| 1 | USER32.dll!SendMessageA | 0xA389C0 | win32_ui.c:imp_SendMessageA |
| 1 | USER32.dll!SystemParametersInfoA | 0x9D4220(GameShutdown) | win32_ui.c:imp_SystemParametersInfoA |
| 1 | ole32.dll!CoUninitialize | 0x9D4220(GameShutdown) | win32_com.c:imp_CoUninitialize |
| 2 | ADVAPI32.dll!RegSetValueExA | 0x943E20 | win32_fs.c:imp_RegSetValueExA |
| 2 | KERNEL32.dll!ExpandEnvironmentStringsA | 0x945170 | win32_mem.c:imp_ExpandEnvironmentStringsA |
| 2 | KERNEL32.dll!FreeEnvironmentStringsA | 0xA76F52(___crtGetEnvironmentStringsA) | win32_mem.c:imp_FreeEnvironmentStringsA |
| 2 | KERNEL32.dll!FreeEnvironmentStringsW | 0xA76F52(___crtGetEnvironmentStringsA) | win32_mem.c:imp_FreeEnvironmentStringsW |
| 2 | KERNEL32.dll!GetACP | 0xA806F0(getSystemCP) | win32_mem.c:imp_GetACP |
| 2 | KERNEL32.dll!GetCPInfo | 0xA8076A | win32_mem.c:imp_GetCPInfo |
| 2 | KERNEL32.dll!GetCurrentProcess | 0xA80269(___report_gsfailure) | win32_mem.c:imp_GetCurrentProcess |
| 2 | KERNEL32.dll!GetCurrentThread | 0xA217A0(WinMain) | win32_sync.c:imp_GetCurrentThread |
| 2 | KERNEL32.dll!GetEnvironmentStrings | 0xA76F52(___crtGetEnvironmentStringsA) | win32_mem.c:imp_GetEnvironmentStrings |
| 2 | KERNEL32.dll!GetEnvironmentStringsW | 0xA76F52(___crtGetEnvironmentStringsA) | win32_mem.c:imp_GetEnvironmentStringsW |
| 2 | KERNEL32.dll!GetFileAttributesA | 0x945170 | win32_fs.c:imp_GetFileAttributesA |
| 2 | KERNEL32.dll!GetFileType | 0xA75C3F(__ioinit) | win32_fs.c:imp_GetFileType |
| 2 | KERNEL32.dll!GetLastError | 0xA76F52(___crtGetEnvironmentStringsA) | win32_core.c:imp_GetLastError |
| 2 | KERNEL32.dll!GetModuleFileNameA | 0xA76E99(__setargv) | win32_fs.c:imp_GetModuleFileNameA |
| 2 | KERNEL32.dll!GetOEMCP | 0xA806F0(getSystemCP) | win32_mem.c:imp_GetOEMCP |
| 2 | KERNEL32.dll!GetStdHandle | 0xA75C3F(__ioinit) | win32_fs.c:imp_GetStdHandle |
| 2 | KERNEL32.dll!HeapCreate | 0xA7712A(__heap_init) | win32_mem.c:imp_HeapCreate |
| 2 | KERNEL32.dll!HeapDestroy | 0xA7712A(__heap_init) | win32_mem.c:imp_HeapDestroy |
| 2 | KERNEL32.dll!InterlockedCompareExchange | 0x9609F0 | win32_sync.c:imp_InterlockedCompareExchange |
| 2 | KERNEL32.dll!IsDebuggerPresent | 0xA80269(___report_gsfailure) | win32_mem.c:imp_IsDebuggerPresent |
| 2 | KERNEL32.dll!IsValidCodePage | 0xA8076A | win32_mem.c:imp_IsValidCodePage |
| 2 | KERNEL32.dll!RaiseException | 0xA6CC7B(__CxxThrowException@8) | win32_core.c:imp_RaiseException |
| 2 | KERNEL32.dll!SetHandleCount | 0xA75C3F(__ioinit) | win32_fs.c:imp_SetHandleCount |
| 2 | KERNEL32.dll!SetThreadAffinityMask | 0xA217A0(WinMain) | win32_sync.c:imp_SetThreadAffinityMask |
| 2 | KERNEL32.dll!TerminateProcess | 0xA80269(___report_gsfailure) | win32_mem.c:imp_TerminateProcess |
| 2 | KERNEL32.dll!TlsAlloc | 0xA73E6E(__mtinit) | win32_sync.c:imp_TlsAlloc |
| 2 | KERNEL32.dll!TlsFree | 0xA73E6E(__mtinit) | win32_sync.c:imp_TlsFree |
| 2 | KERNEL32.dll!TlsSetValue | 0xA73E6E(__mtinit) | win32_sync.c:imp_TlsSetValue |
| 2 | KERNEL32.dll!UnhandledExceptionFilter | 0xA80269(___report_gsfailure) | win32_mem.c:imp_UnhandledExceptionFilter |
| 2 | KERNEL32.dll!WideCharToMultiByte | 0xA76F52(___crtGetEnvironmentStringsA) | win32_mem.c:imp_WideCharToMultiByte |
| 2 | SHELL32.dll!ShellExecuteA | 0x945170 | win32_fs.c:imp_ShellExecuteA |
| 2 | USER32.dll!AdjustWindowRect | 0xA37D90(D3DCreateDevice) | win32_ui.c:imp_AdjustWindowRect |
| 2 | USER32.dll!GetSystemMetrics | 0x945170 | win32_ui.c:imp_GetSystemMetrics |
| 2 | USER32.dll!GetWindowLongA | 0xA37D90(D3DCreateDevice) | win32_ui.c:imp_GetWindowLongA |
| 2 | USER32.dll!MoveWindow | 0xA37D90(D3DCreateDevice) | win32_ui.c:imp_MoveWindow |
| 2 | USER32.dll!SetWindowPos | 0xA37D90(D3DCreateDevice) | win32_ui.c:imp_SetWindowPos |
| 2 | ole32.dll!CoInitializeEx | 0xA217A0(WinMain) | win32_com.c:imp_CoInitializeEx |
| 3 | KERNEL32.dll!ExitProcess | 0xA6D10D | win32_mem.c:imp_ExitProcess |
| 3 | KERNEL32.dll!MultiByteToWideChar | 0xA21630(CheckGameExplorerAccess) | win32_mem.c:imp_MultiByteToWideChar |
| 3 | KERNEL32.dll!OutputDebugStringA | 0xA21590 | win32_mem.c:imp_OutputDebugStringA |
| 3 | KERNEL32.dll!SetLastError | 0xA73C49 | win32_core.c:imp_SetLastError |
| 3 | KERNEL32.dll!VirtualFree | 0xA79AF4(___sbh_free_block) | win32_mem.c:imp_VirtualFree |
| 3 | OLEAUT32.dll!#2 | 0xA21630(CheckGameExplorerAccess) | win32_com.c:imp_SysAllocString |
| 3 | OLEAUT32.dll!#6 | 0xA21630(CheckGameExplorerAccess) | win32_com.c:imp_SysFreeString |
| 3 | USER32.dll!MessageBoxA | 0xA21630(CheckGameExplorerAccess) | win32_ui.c:imp_MessageBoxA |
| 3 | ole32.dll!CoCreateInstance | 0xA21630(CheckGameExplorerAccess) | win32_com.c:imp_CoCreateInstance |
| 3 | ole32.dll!CoInitialize | 0xA21630(CheckGameExplorerAccess) | win32_com.c:imp_CoInitialize |
| 4 | GDI32.dll!CreateDCA | 0xA41730 | win32_ui.c:imp_CreateDCA |
| 4 | GDI32.dll!DeleteDC | 0xA41730 | win32_ui.c:imp_DeleteDC |
| 4 | GDI32.dll!ExtEscape | 0xA41730 | win32_ui.c:imp_ExtEscape |
| 4 | KERNEL32.dll!DeleteCriticalSection | 0xA79868(__mtdeletelocks) | win32_sync.c:imp_DeleteCriticalSection |
| 4 | KERNEL32.dll!FreeLibrary | 0xA417E0 | win32_core.c:imp_FreeLibrary |
| 4 | KERNEL32.dll!HeapReAlloc | 0xA79E08(___sbh_alloc_new_region) | win32_mem.c:imp_HeapReAlloc |
| 4 | KERNEL32.dll!HeapSize | 0xA741EA(__msize) | win32_mem.c:imp_HeapSize |
| 4 | KERNEL32.dll!LoadLibraryA | 0xA41A6A | win32_core.c:imp_LoadLibraryA |
| 4 | KERNEL32.dll!VirtualAlloc | 0xA79E08(___sbh_alloc_new_region) | win32_mem.c:imp_VirtualAlloc |
| 4 | KERNEL32.dll!WriteFile | 0xA75881 | win32_fs.c:imp_WriteFile |
| 4 | d3dx9_41.dll!D3DXMatrixMultiply | 0xA96084 | gen_d3d9_guest.c:thunk_D3DXMatrixMultiply |
| 5 | KERNEL32.dll!GetStringTypeA | 0xA82403(__crtGetStringTypeA_stat) | win32_mem.c:imp_GetStringTypeA |
| 5 | KERNEL32.dll!GetStringTypeW | 0xA82403(__crtGetStringTypeA_stat) | win32_mem.c:imp_GetStringTypeW |
| 5 | KERNEL32.dll!LCMapStringA | 0xA70B57(__crtLCMapStringA_stat) | win32_mem.c:imp_LCMapStringA |
| 5 | KERNEL32.dll!LCMapStringW | 0xA70B57(__crtLCMapStringA_stat) | win32_mem.c:imp_LCMapStringW |
| 6 | KERNEL32.dll!CloseHandle | 0xA7605A | win32_core.c:imp_CloseHandle |
| 6 | KERNEL32.dll!CreateEventA | 0xA3B490(StartRenderThread) | win32_sync.c:imp_CreateEventA |
| 6 | KERNEL32.dll!CreateMutexA | 0x945090(ShowGameWindow) | win32_sync.c:imp_CreateMutexA |
| 6 | KERNEL32.dll!GetLocaleInfoA | 0xA8220A(___ansicp) | win32_mem.c:imp_GetLocaleInfoA |
| 6 | KERNEL32.dll!GetProcessAffinityMask | 0x92A020(TimerCalibrate) | win32_sync.c:imp_GetProcessAffinityMask |
| 6 | KERNEL32.dll!GetUserDefaultLangID | 0x96FD80(ReadLanguageSetting) | win32_mem.c:imp_GetUserDefaultLangID |
| 6 | KERNEL32.dll!GetUserGeoID | 0x96FD80(ReadLanguageSetting) | win32_mem.c:imp_GetUserGeoID |
| 6 | KERNEL32.dll!QueryPerformanceFrequency | 0x92A020(TimerCalibrate) | win32_sync.c:imp_QueryPerformanceFrequency |
| 6 | KERNEL32.dll!ReleaseMutex | 0x945090(ShowGameWindow) | win32_sync.c:imp_ReleaseMutex |
| 6 | KERNEL32.dll!ResumeThread | 0xA3B490(StartRenderThread) | win32_sync.c:imp_ResumeThread |
| 6 | KERNEL32.dll!SetProcessAffinityMask | 0x92A020(TimerCalibrate) | win32_sync.c:imp_SetProcessAffinityMask |
| 6 | USER32.dll!CreateWindowExA | 0x9EB2F0(CreateGameWindow) | win32_ui.c:imp_CreateWindowExA |
| 6 | USER32.dll!EnumDisplayMonitors | 0x9EB2F0(CreateGameWindow) | win32_ui.c:imp_EnumDisplayMonitors |
| 6 | USER32.dll!IsIconic | 0x945090(ShowGameWindow) | win32_ui.c:imp_IsIconic |
| 6 | USER32.dll!LoadCursorA | 0x9EB2F0(CreateGameWindow) | win32_ui.c:imp_LoadCursorA |
| 6 | USER32.dll!LoadIconA | 0x9EB2F0(CreateGameWindow) | win32_ui.c:imp_LoadIconA |
| 6 | USER32.dll!MessageBoxW | 0x9EAF10(DiscCheck) | win32_ui.c:imp_MessageBoxW |
| 6 | USER32.dll!RegisterClassExA | 0x9EB2F0(CreateGameWindow) | win32_ui.c:imp_RegisterClassExA |
| 6 | USER32.dll!SetForegroundWindow | 0x945090(ShowGameWindow) | win32_ui.c:imp_SetForegroundWindow |
| 6 | USER32.dll!ShowWindow | 0x945090(ShowGameWindow) | win32_ui.c:imp_ShowWindow |
| 6 | USER32.dll!UpdateWindow | 0x945090(ShowGameWindow) | win32_ui.c:imp_UpdateWindow |
| 6 | WS2_32.dll!#115 | 0x93C3E0(NetInitWinsock) | win32_net.c:imp_WSAStartup |
| 6 | WS2_32.dll!#116 | 0x93C3E0(NetInitWinsock) | win32_net.c:imp_WSACleanup |
| 6 | WS2_32.dll!#12 | 0x93C3E0(NetInitWinsock) | win32_net.c:imp_inet_ntoa |
| 6 | WS2_32.dll!#52 | 0x93C3E0(NetInitWinsock) | win32_net.c:imp_gethostbyname |
| 6 | WS2_32.dll!#57 | 0x93C3E0(NetInitWinsock) | win32_net.c:imp_gethostname |
| 7 | KERNEL32.dll!CreateThread | 0x98FD20(NuThreadCreate) | win32_sync.c:imp_CreateThread |
| 7 | KERNEL32.dll!GetConsoleCP | 0xA7F18D | win32_fs.c:imp_GetConsoleCP |
| 7 | KERNEL32.dll!GetConsoleMode | 0xA7F18D | win32_fs.c:imp_GetConsoleMode |
| 7 | KERNEL32.dll!GetDriveTypeA | 0x9982E0(FindGameDisc) | win32_fs.c:imp_GetDriveTypeA |
| 7 | KERNEL32.dll!GetLogicalDrives | 0x9982E0(FindGameDisc) | win32_fs.c:imp_GetLogicalDrives |
| 7 | KERNEL32.dll!GetVolumeInformationA | 0x9982E0(FindGameDisc) | win32_fs.c:imp_GetVolumeInformationA |
| 7 | KERNEL32.dll!SetFilePointer | 0xA87F35 | win32_fs.c:imp_SetFilePointer |
| 7 | KERNEL32.dll!SetStdHandle | 0xA80F0C | win32_fs.c:imp_SetStdHandle |
| 7 | KERNEL32.dll!SetThreadPriority | 0x98FD20(NuThreadCreate) | win32_sync.c:imp_SetThreadPriority |
| 7 | SHELL32.dll!SHCreateDirectoryExA | 0x945250(GetProfileFolder) | win32_fs.c:imp_SHCreateDirectoryExA |
| 7 | SHELL32.dll!SHGetFolderPathA | 0x945250(GetProfileFolder) | win32_fs.c:imp_SHGetFolderPathA |
| 7 | USER32.dll!GetKeyboardLayout | 0x9400B0 | win32_ui.c:imp_GetKeyboardLayout |
| 7 | USER32.dll!GetWindowThreadProcessId | 0x9400B0 | win32_ui.c:imp_GetWindowThreadProcessId |
| 7 | d3d9.dll!Direct3DCreate9 | 0xA960D2 | gen_d3d9_guest.c:imp_Direct3DCreate9 |
| 8 | DINPUT8.dll!DirectInput8Create | 0xAC2410 | win32_input.c:imp_DirectInput8Create |
| 8 | KERNEL32.dll!CreateFileA | 0xA8F71B | win32_fs.c:imp_CreateFileA |
| 8 | KERNEL32.dll!DebugBreak | 0x919A10 | win32_mem.c:imp_DebugBreak |
| 8 | KERNEL32.dll!GetConsoleOutputCP | 0xA90DB1(__putwch_nolock) | win32_fs.c:imp_GetConsoleOutputCP |
| 8 | KERNEL32.dll!SetEvent | 0xA3B510 | win32_sync.c:imp_SetEvent |
| 8 | KERNEL32.dll!TryEnterCriticalSection | 0x9A2540 | win32_sync.c:imp_TryEnterCriticalSection |
| 8 | KERNEL32.dll!WriteConsoleA | 0xA90DB1(__putwch_nolock) | win32_fs.c:imp_WriteConsoleA |
| 8 | KERNEL32.dll!WriteConsoleW | 0xA90DB1(__putwch_nolock) | win32_fs.c:imp_WriteConsoleW |
| 8 | USER32.dll!DispatchMessageA | 0x9D42E0(PumpMessages) | win32_ui.c:imp_DispatchMessageA |
| 8 | USER32.dll!PeekMessageA | 0x9D42E0(PumpMessages) | win32_ui.c:imp_PeekMessageA |
| 8 | USER32.dll!TranslateMessage | 0x9D42E0(PumpMessages) | win32_ui.c:imp_TranslateMessage |
| 8 | WS2_32.dll!#16 | 0x9D2F70 | win32_net.c:imp_recv |
| 8 | WS2_32.dll!#19 | 0x9D2F70 | win32_net.c:imp_send |
| 8 | WS2_32.dll!#21 | 0x9D2F70 | win32_net.c:imp_setsockopt |
| 8 | XINPUT1_3.dll!#2 | 0xAC2422 | win32_input.c:imp_XInputGetState |
| 8 | XINPUT1_3.dll!#4 | 0xAC241C | win32_input.c:imp_XInputGetCapabilities |
| 9 | KERNEL32.dll!DeleteFileA | 0x9F6100 | win32_fs.c:imp_DeleteFileA |
| 9 | KERNEL32.dll!ReadFile | 0xA7630A | win32_fs.c:imp_ReadFile |
| 9 | KERNEL32.dll!SetEndOfFile | 0xA946FD(__chsize_nolock) | win32_fs.c:imp_SetEndOfFile |
| 9 | WS2_32.dll!#11 | 0x93C740 | win32_net.c:imp_inet_addr |
| 9 | WS2_32.dll!#111 | 0x93C740 | win32_net.c:imp_WSAGetLastError |
| 9 | WS2_32.dll!#3 | 0x96AF50 | win32_net.c:imp_closesocket |
| 9 | WS2_32.dll!#4 | 0x93C740 | win32_net.c:imp_connect |
| 9 | WS2_32.dll!#9 | 0x93C740 | win32_net.c:imp_htons |
| 9 | X3DAudio1_6.dll!X3DAudioInitialize | 0x91B8E0 | win32_audio.c:imp_X3DAudioInitialize |
| 9 | d3dx9_41.dll!D3DXCompileShader | 0xA960CC | gen_d3d9_guest.c:thunk_D3DXCompileShader |
| 10 | KERNEL32.dll!CreateDirectoryA | 0x9453F0 | win32_fs.c:imp_CreateDirectoryA |
| 10 | WS2_32.dll!#10 | 0x93C550 | win32_net.c:imp_ioctlsocket |
| 10 | WS2_32.dll!#23 | 0x93C550 | win32_net.c:imp_socket |
| 10 | XINPUT1_3.dll!#3 | 0xAC2416 | win32_input.c:imp_XInputSetState |
| 10 | d3dx9_41.dll!D3DXGetShaderConstantTable | 0xA960C6 | gen_d3d9_guest.c:thunk_D3DXGetShaderConstantTable |
| 10 | d3dx9_41.dll!D3DXSaveSurfaceToFileInMemory | 0xA960C0 | gen_d3d9_guest.c:thunk_D3DXSaveSurfaceToFileInMemory |
| 12 | KERNEL32.dll!EnumSystemLocalesA | 0xA849B3 | win32_mem.c:imp_EnumSystemLocalesA |
| 12 | KERNEL32.dll!GetUserDefaultLCID | 0xA849B3 | win32_mem.c:imp_GetUserDefaultLCID |
| 12 | KERNEL32.dll!IsValidLocale | 0xA849B3 | win32_mem.c:imp_IsValidLocale |

## Unresolved indirect tail jumps reached (24)

| d | Target expression | Near | Chain |
|---|---|---|---|
| 1 | `eax` | 0xA30216 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA2F650 |
| 3 | `eax` | 0xA6CC51 | xi init: 0xA68198(___onexitinit) > 0xA719F6(__calloc_crt) > 0xA6E18C(__calloc_impl) > 0xA6CC3E |
| 4 | `GLD32(0xED9E80)` | 0xA4628E | WndProc: 0x9EAF50(TTalesWindow_WndProc) > 0x9D4220(GameShutdown) > .. > 0xA4628E |
| 4 | `GLD32(0xED97E4)` | 0xA41B08 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > .. > 0xA41B08 |
| 4 | `GLD32(0xED97BC)` | 0xA41AEA | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > .. > 0xA41AEA |
| 4 | `GLD32(0xED9994)` | 0xA41C4C | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > .. > 0xA41C4C |
| 4 | `GLD32(0xED97EC)` | 0xA41B0E | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > .. > 0xA41B0E |
| 4 | `GLD32(0xED97CC)` | 0xA41AF6 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > .. > 0xA41AF6 |
| 4 | `GLD32(0xED99A4)` | 0xA41C58 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > .. > 0xA41C58 |
| 4 | `GLD32(eax * 4 + 0xAA279A)` | 0xA969C7 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > .. > 0xA9684A |
| 5 | `GLD32(ebx)` | 0xA775AB | xc init: 0xC3C200 > 0x684530(PhysicsEffects::ctor_or_dtor) > .. > 0xA77590(__trandisp1) |
| 5 | `GLD32(eax * 4 + 0xAA420A)` | 0xA99043 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > .. > 0xA99024 |
| 5 | `GLD32(eax * 4 + 0xAA32B2)` | 0xA98A6C | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > .. > 0xA98A4B |
| 5 | `GLD32(eax * 4 + 0xAA29A6)` | 0xA973F6 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > .. > 0xA9720B |
| 5 | `GLD32(eax * 4 + 0xAA2816)` | 0xA96C1F | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > .. > 0xA96B2D |
| 5 | `GLD32(eax * 4 + 0xAA2C76)` | 0xA97B53 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > .. > 0xA97B30 |
| 5 | `GLD32(eax * 4 + 0xAA27C2)` | 0xA96A4A | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > .. > 0xA96A3C |
| 5 | `GLD32(eax * 4 + 0xAA2862)` | 0xA96DFF | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > .. > 0xA96DDE |
| 6 | `GLD32(eax + 0x28)` | 0xA2CB33 | CRT entry: 0xA69393(WinMainCRTStartup) > 0xA691B3(__tmainCRTStartup) > .. > 0xA2C8A0(D3DCheckShaderModel2) |
| 6 | `GLD32(eax * 4 + 0xAA4A2E)` | 0xA99633 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > .. > 0xA99623 |
| 6 | `GLD32(eax * 4 + 0xAA45E2)` | 0xA99157 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > .. > 0xA99149 |
| 6 | `GLD32(eax * 4 + 0xAA2A0A)` | 0xA975C1 | thread Render: 0xA3B3D0(RenderThreadProc) > 0xA389C0 > .. > 0xA974CF |
| 7 | `GLD32(ecx * 4 + 0x96D828)` | 0x96D7DD | CRT entry: 0xA69393(WinMainCRTStartup) > 0xA691B3(__tmainCRTStartup) > .. > 0x96D690 |
| 8 | `eax` | 0x9640F4 | CRT entry: 0xA69393(WinMainCRTStartup) > 0xA691B3(__tmainCRTStartup) > .. > 0x9640E0 |

Calls through global pointers (not followed): [0xD1FBC0] `_FPinit` and [0x10DD0E0] TLS callback in `_cinit` (d=2), [0xF7FE78]/[0xF7FE7C] in 0x4C2E10, 0x8DE460, 0x4C2EB0 (d=7-11).

## Dropped starts that code or data still points at (addition)

Script: job tmp `boot_reach/dropped.py`, output `dropped.txt`.

Result: **no dropped start has a real pointer reference.** The CFG list drops 7256 of the 51670 starts in `generated.nosync/inputs/functions.json` (44575 kept).

| Check | Hits | What they are |
|---|---|---|
| Constant in the 2038 reached functions (labels, comments, return addresses, call sites, switch-arm compares and direct call targets removed) | 1 | 0x7FFFFF in 0x960CE0: a mask, not a pointer |
| Aligned dword in a non-code section (.rdata, .data, .idata, .shr, CONST; .rsrc skipped) | 334 starts | 147 ASCII (`"tan\0"` = 0x6E6174 and `"sin"` in the `___libm_error_support` name table 0xD20F1C; `"PSP\0"` = 0x505350), 161 UTF-16 text (`L"AB"` = 0x420041 inside `L"AVAILABLE"` etc.), 26 integer constants (0x480001, 0x41FFFE, 0xAAAAAB, 0x7FFFFF) |
| Of those 334, a slot next to a non-text code pointer (vtable or table shape) | 1 | 0x53575F at 0xC86224: `"_WS\0"` followed by `"Join"`; stub_triage already showed 0x53575F is int3 padding |
| Of those 334, a slot in a table that reached code references | 0 (excluding the 3 text hits above) | - |

Not checked: pointers computed at run time, and constants in functions this scan does not reach (virtual calls are not followed).

Ranked by distance there is nothing to rank: the only hits are at d=2 (0x7FFFFF mask in 0x960CE0), d=6 (libm name strings at 0xD20F1C) and d=7 ("PSP" at 0xC4C3C8 from 0x5C1EF0). All are false positives.

### What the runtime does on an indirect call to a non-start

The runtime does not continue silently. It prints the address and exits with status 3 (engine `tools/xrecomp/include/recomp_types.h`, `runtime/src/recomp_glue.c` on feat/pe-lift-gaps in `<work>/MacRecomp-hp`):

- `RECOMP_ICALL_SAFE_AT` / `_AT_CC` (recomp_types.h:1693-1737): if `RECOMP_ICALL_IS_CODE(va)` fails, it calls `recomp_icall_not_code_log` ("indirect call to non-code VA 0x%08X", recomp_glue.c:135). Otherwise it tries `recomp_lookup_manual`, then `recomp_lookup` (an exact match in the dispatch table, translator.py:3857), then `recomp_lookup_kernel` (host thunks). If all three miss, it calls `recomp_icall_fail_log`.
- `RECOMP_ITAIL` (recomp_types.h:1744-1756): the same lookups, with no code-range check.
- `recomp_icall_fail_log` (recomp_glue.c:128-133) prints "indirect call to unresolved VA 0x%08X", calls `recomp_caller_log()` and then `exit(3)`. The `g_esp`/`g_eax = 0` clean-up after it in the macros never runs. Boot run 5 stopped this way at 0x00A68198 (notes/boot_log.md, Failure 4).
- An address inside a lifted function is not a table entry, so it takes the same exit. Only *direct* jumps to non-starts become silent stubs (`lhp_stubs_unresolved.c`).
