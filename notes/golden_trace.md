# Golden trace: the original exe under Wine

Wine is a reference oracle here (test only, not the product). The trace lists
every Win32 call the ORIGINAL `LEGOHarryPotter.exe` makes (sha256 21c2d30c...,
same as `orig.nosync/lhp_win32.exe`) from start-up through its first frames.
Compare it with our boot's import trace to find the first divergence.

Files (outside git):
- `~/Developer/hp-wine-oracle/trace/golden.log`: raw Wine log, 259 MB.
- `~/Developer/hp-wine-oracle/trace/golden.jsonl`: filtered, 491,241 events, 81 MB.
- `run1_gl_crash.log`, `run2_vk_createdevice_fail.log`, `run3.log`: failed and earlier runs (evidence below).

Tools: `tools/trace/filter_relay.py` (log -> JSONL + milestone summary),
`tools/trace/compare.py` (golden vs ours).

## Recipe

- Game: APFS clone `cp -cR "<owner prefix>/.../common/LEGO Harry Potter" ~/Developer/hp-wine-oracle/game` (used about 156 KB of new space).
- Prefix: `WINEPREFIX=~/Developer/hp-wine-oracle/prefix`, `wineboot -i`, Wine Staging 11.18 (wow64) from `~/wine-11.3/Wine Staging.app`. Builtin d3d9 (wined3d), default **GL** renderer. No DXVK or DXMT.
- Registry (HKCU): `Software\Wine\Drivers Audio=""` (no sound). `Software\Wine\Debug RelayFromInclude=LEGOHarryPotter.exe` and `RelayExclude=<Wine default list>`. **Delete the default `RelayFromExclude`**: when an exclude list exists, Wine ignores the include list.
- Run, from the game folder:
  `WINEDEBUG=+relay,+seh,+loaddll,+threadname,+timestamp,+d3d9 perl -e 'alarm 90; exec @ARGV' wine .../game/LEGOHarryPotter.exe -Windowed > trace/golden.log 2>&1; wineserver -k`
  (`-Windowed` is a game flag, `docs/STARTUP_REQS.md:86`; our boot does not pass it.)
- Filter: `python -I tools/trace/filter_relay.py trace/golden.log trace/golden.jsonl --summary OUT.md` (10 s).

## Configuration attempts

| Run | Renderer | Relay scope | Result |
|---|---|---|---|
| 1 | GL | all modules (`RelayFromExclude` active) | Crash inside `Direct3DCreate9` (0xA2C7EB): wined3d's GL format probe `glGetTexImage(..., 0x0026BA0C)` overwrote the stack; return to 0x6666666666666666, "Exception frame is not in stack limits". |
| 2 | Vulkan (MoltenVK) | exe only | `CreateDevice` called, then fails: `wined3d_context_vk_create_bo Failed to create Vulkan buffer, VK_ERROR_FEATURE_NOT_PRESENT` (MoltenVK supports only MTLBUFFER/MTLHEAP external memory). Exit 3, no Present. |
| 3, golden | GL | exe only | Device created, 539-1197 Presents. |

Runs 1 and 3 differ only in relay scope, so relaying wined3d's own GL calls caused the run 1 crash.

## Wine-side end: NULL read at 0x91B0AD

Both GL runs end with `Unhandled page fault on read access to 00000000 at address 0091B0AD` on the main thread (ecx=0, edx=3F800004). It happened at t=51.8 s (run 3) and t=29.5 s (golden), after hundreds of frames. The game's SEH handlers at 0xC31203 and 0xC2FB18 run but do not recover. Wine starts `winedbg --auto`, which writes only text. No macOS crash report appeared. Cause not investigated; it does not affect the start-up trace.

## Milestones (golden run, t from the first exe call)

| Milestone | seq | t (s) | tid | caller | detail |
|---|---|---|---|---|---|
| first call | 223 | 0.000 | 00f4 | 00A77239 | `GetSystemTimeAsFileTime(0026ff0c) -> 00000000` |
| CreateThread Render (tid 0144) | 1791 | 0.064 | 00f4 | 0098FDD3 | `CreateThread(00000000, 00010000, 00967410, 00ed6e48, 00000004, 00ed6ef8) -> 000000a8` |
| Direct3DCreate9 | 1811 | 0.078 | 00f4 | 00A2C7EB | `Direct3DCreate9(00000020) -> 01400828` |
| CreateThread m_readThread (tid 0148) | 1856 | 1.516 | 00f4 | 0098FDD3 | `CreateThread(00000000, 00000000, 00967410, 00ed6efc, 00000004, 00ed6fac) -> 000000d0` |
| CreateThread m_decodeThread (tid 014c) | 1864 | 1.516 | 00f4 | 0098FDD3 | `CreateThread(00000000, 00000000, 00967410, 00ed6fb0, 00000004, 00ed7060) -> 000000dc` |
| first GAME.DAT open | 1924 | 1.523 | 00f4 | 009A7F1D | `CreateFileA(0026f7ac "GAME.DAT", 80000000, 00000001, 00000000, 00000003, 00000000, 00000000) -> 000000fc` |
| first ReadFile on GAME.DAT | 1926 | 1.525 | 00f4 | 00966FC7 | `ReadFile(000000fc, 0026fbe4, 00000008, 0026fbbc, 00000000) -> 00000001` |
| first CreateWindowExA | 2041 | 1.538 | 00f4 | 009EB45F | `CreateWindowExA(00000000, 00d1bd94 "TTalesWindow", 00c8c250 "LEGO\xae Harry Potter\x99", 00cf0000, fffffffc, ffffffe2, 00000408, 00000322, 00000000, 0` |
| CreateThread TextureDecode (tid 0150) | 5073 | 1.690 | 00f4 | 0098FDD3 | `CreateThread(00000000, 00000000, 00967410, 00ed7064, 00000004, 00ed7114) -> 00000120` |
| CreateDevice | 5086 | 1.691 | 00f4 |  | `d3d9_CreateDevice(iface 01400828, adapter 0, device_type 0x1, focus_window 00020064, flags 0x50, parameters 010B100C, device 010B0ED4.) -> None` |
| CreateDevice returned | 5089 | 1.806 | 00f4 |  | `d3d9_CreateDevice(Created device 014BFED0.) -> None` |
| first Present | 5153 | 1.816 | 00f4 |  | `d3d9_device_Present(iface 014BFED0, src_rect (null), dst_rect (null), dst_window_override 00000000, dirty_region 00000000.) -> None` |
| CreateThread MusicStream (tid 015c) | 268391 | 4.381 | 00f4 | 0098FDD3 | `CreateThread(00000000, 00000000, 00967410, 00ed7118, 00000004, 00ed71c8) -> 00000178` |
| CreateThread MusicStream (tid 0160) | 268443 | 4.384 | 00f4 | 0098FDD3 | `CreateThread(00000000, 00000000, 00967410, 00ed71cc, 00000004, 00ed727c) -> 0000019c` |
| CreateThread MusicStream (tid 0164) | 268495 | 4.385 | 00f4 | 0098FDD3 | `CreateThread(00000000, 00000000, 00967410, 00ed7280, 00000004, 00ed7330) -> 000001c0` |
| CreateThread bgprocThread (tid 0168) | 268604 | 4.411 | 00f4 | 0098FDD3 | `CreateThread(00000000, 00010000, 00967410, 00ed7334, 00000004, 00ed73e4) -> 00000204` |
| CreateThread SaveCore (tid 017c) | 278891 | 5.190 | 00f4 | 0098FDD3 | `CreateThread(00000000, 00008000, 00967410, 00ed73e8, 00000004, 00ed7498) -> 0000023c` |
| CreateThread NuFileStream (tid 0180) | 324826 | 7.705 | 0168 | 0098FDD3 | `CreateThread(00000000, 00000000, 00967410, 00ed749c, 00000004, 00ed754c) -> 00000260` |
| CreateThread NuFileStream (tid 0184) | 326249 | 7.809 | 0168 | 0098FDD3 | `CreateThread(00000000, 00000000, 00967410, 00ed749c, 00000004, 00ed754c) -> 00000280` |
| CreateThread NuFileStream (tid 0188) | 333171 | 8.114 | 0168 | 0098FDD3 | `CreateThread(00000000, 00000000, 00967410, 00ed749c, 00000004, 00ed754c) -> 000002a0` |
| CreateThread NuFileStream (tid 018c) | 484239 | 29.155 | 0168 | 0098FDD3 | `CreateThread(00000000, 00000000, 00967410, 00ed749c, 00000004, 00ed754c) -> 000002cc` |
| first unhandled-looking exception | 491404 | 29.527 | 00f4 | 0091B0AD | `c0000005(EXCEPTION_ACCESS_VIOLATION, 0091B0AD) -> None` |

Present calls: 539 (first at t=1.816 s, last at t=29.518 s)

Threads in order of first event:

| tid | name | events | first seq |
|---|---|---|---|
| 00f4 | main | 360808 | 146 |
| 0144 | Render | 1 | 1804 |
| 0148 | m_readThread | 15303 | 1868 |
| 014c | m_decodeThread | 14897 | 1889 |
| 0150 | TextureDecode | 5725 | 5082 |
| 015c | MusicStream | 800 | 268410 |
| 0160 | MusicStream | 2 | 268463 |
| 0164 | MusicStream | 2 | 268503 |
| 0168 | bgprocThread | 93412 | 268613 |
| 017c | SaveCore | 163 | 278898 |
| 0180 | NuFileStream | 33 | 324834 |
| 0184 | NuFileStream | 24 | 326257 |
| 0188 | NuFileStream | 33 | 333186 |
| 018c | NuFileStream | 12 | 484247 |

Notes:
- Every thread starts through the wrapper 0x98FD20 (CreateThread return address 0x98FDD3) with the trampoline start address **0x967410**. The real routine is in the parameter block (args[3]), which relay does not show. The names match `docs/STARTUP_REQS.md` section 5 (Render, m_readThread, m_decodeThread, TextureDecode, MusicStream x3, bgprocThread, SaveCore, NuFileStream from bgprocThread).
- Order of the first frames: Render thread, then `Direct3DCreate9` (t=0.078), then m_read/m_decode threads, GAME.DAT open/read (t=1.52), `CreateWindowExA` (t=1.54), TextureDecode, `CreateDevice` (HAL, flags 0x50 = PUREDEVICE|HARDWARE_VERTEXPROCESSING, t=1.69), first Present on the main thread (t=1.82). This confirms the `STARTUP_REQS.md:201` OPEN item: device type 1 (HAL).
- Wine never relays `GetCurrentProcessId`, `GetCurrentThreadId`, `GetCurrentProcess` or `GetCurrentThread` (`-norelay`). Direct3D 9 vtable calls are not relayed; the `d3d9.com` events come from the `+d3d9` channel.
- `seq` is a record index before helper-process threads are removed, so it has gaps. It still increases in log order.

## Compare tool

```
python -I tools/trace/compare.py ~/Developer/hp-wine-oracle/trace/golden.jsonl <job tmp>/boot/runN.log [--thread main|Render|m_readThread|<tid>] [--context 8] [--ignore A,B] [--keep A,B] [--rets 5]
```

Method: per thread, drop timing/lock calls and the `-norelay` calls, map Wine
forward names to import names (`RtlAllocateHeap` -> `HeapAlloc`, ...), collapse
runs of the same key, then align on (function, return address) with difflib.
Output: return-value differences in the matched part (0 on one side only, or
two small values that differ), the first divergence with context, or "ours
ends after golden event X".

Result on `run6.log` (gen_fix boot, 830 imports): 135 of 178 events match.
- Return differences: `GetProcAddress` returns 0 for FlsAlloc/FlsGetValue/FlsSetValue/FlsFree (0xA73E98..0xA73EBF) and EncodePointer (0xA73A53). Wine returns real addresses.
- First divergence: after `GetModuleHandleA("KERNEL32.DLL")` at 0xA73A38, our side makes an extra call `GetModuleHandleA(NULL)` (return address 0xA739BB) before `GetProcAddress("EncodePointer")`. Wine goes directly to `GetProcAddress` and then calls `EncodePointer` at 0xA73A5D. Not investigated further (owner: boot lane).

## Logging spec needed on our side

Today `hp_boot.c:47-60` prints `IMP n Func(a0 a1 a2 a3) ret RET -> EAX`: no thread, no DLL, four fixed slots, and IAT slots only (`resolve_traced`, `hp_boot.c:73-88`). compare.py already reads the format below; each part is optional.

1. Format: `IMP <n> t=<tid> <DLL>!<Func>(<a0> ... <aK-1>) ret <RET> -> <EAX>`. `tid` = the guest thread id that `GetCurrentThreadId` returns on that thread, as 4+ hex digits. K = the export's real argument count (from the export table nargs, `notes/nargs_check.md`), not 4. `n` stays a global atomic taken at entry (compare.py sorts by n).
2. Thread names: in the RaiseException thunk, when code = 0x406D1388 and THREADNAME_INFO.dwType = 0x1000, print `THR <tid> <name>` (dwThreadID = -1 means the current thread). This maps our threads to Wine's by name (`--thread Render`).
3. GetProcAddress results: wrap each non-zero VA that GetProcAddress returns with a trampoline from the same pool, named `<DLL>!<Func>`, so calls through function pointers (EncodePointer, Fls*, d3dx, xinput) are traced like IAT calls. `TRACE_SLOTS` (256) may need to grow; IAT plus GetProcAddress names stay below about 400.
4. Optional, for the frame milestones: one line per Direct3D 9 COM call in the guest COM layer (`gen_d3d9_guest.c`): `COM <n> t=<tid> d3d9!IDirect3DDevice9::<Method>(...) ret <RET> -> <HRESULT>`. compare.py ignores COM lines today; the milestones need CreateDevice and Present only.

## Trace head (first 2,000 events, loaddll lines removed)

Columns: seq, tid, return address, dll.func(args) -> return value.

```
   223 00f4 00A77239 kernel32.GetSystemTimeAsFileTime(0026ff0c) -> 00000000
   224 00f4 00A77255 kernel32.GetTickCount() -> 12fe19a3
   225 00f4 00A77261 kernel32.QueryPerformanceCounter(0026ff04) -> 00000001
   226 00f4 00A691CD kernel32.GetStartupInfoA(0026fea8) -> 7bec40c0
   227 00f4 00A691E4 kernel32.GetProcessHeap() -> 00150000
   228 00f4 00A691EB ntdll.RtlAllocateHeap(00150000, 00000000, 00000094) -> 0015bf88
   229 00f4 00A69207 kernel32.GetVersionExA(0015bf88) -> 00000001
   230 00f4 00A69239 kernel32.GetProcessHeap() -> 00150000
   231 00f4 00A69240 kernel32.HeapFree(00150000, 00000000, 0015bf88) -> 003b0001
   232 00f4 00A77141 kernel32.HeapCreate(00000000, 00001000, 00000000) -> 024a0000
   233 00f4 00A73E7A kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   234 00f4 00A73E98 kernel32.GetProcAddress(7beb0000, 00d206e8 "FlsAlloc") -> 7beb4570
   235 00f4 00A73EA5 kernel32.GetProcAddress(7beb0000, 00d206dc "FlsGetValue") -> 7bebcb50
   236 00f4 00A73EB2 kernel32.GetProcAddress(7beb0000, 00d206d0 "FlsSetValue") -> 7bebcb64
   237 00f4 00A73EBF kernel32.GetProcAddress(7beb0000, 00d206c8 "FlsFree") -> 7beb4590
   238 00f4 00A73F13 kernel32.TlsAlloc() -> 00000017
   239 00f4 00A73A38 kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   240 00f4 00A73A53 kernel32.GetProcAddress(7beb0000, 00d20698 "EncodePointer") -> 7bf45e50
   241 00f4 00A73A5D ntdll.RtlEncodePointer(00000000) -> fcb8f73e
   242 00f4 00A73A38 kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   243 00f4 00A73A53 kernel32.GetProcAddress(7beb0000, 00d20698 "EncodePointer") -> 7bf45e50
   244 00f4 00A73A5D ntdll.RtlEncodePointer(00a73ff2) -> 6341f76d
   245 00f4 00A73A38 kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   246 00f4 00A73A53 kernel32.GetProcAddress(7beb0000, 00d20698 "EncodePointer") -> 7bf45e50
   247 00f4 00A73A5D ntdll.RtlEncodePointer(00a6d486) -> 96fbf76d
   248 00f4 00A73A38 kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   249 00f4 00A73A53 kernel32.GetProcAddress(7beb0000, 00d20698 "EncodePointer") -> 7bf45e50
   250 00f4 00A73A5D ntdll.RtlEncodePointer(7beb4570) -> 5e00cacb
   251 00f4 00A73A38 kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   252 00f4 00A73A53 kernel32.GetProcAddress(7beb0000, 00d20698 "EncodePointer") -> 7bf45e50
   253 00f4 00A73A5D ntdll.RtlEncodePointer(7bebcb50) -> 1910cacb
   254 00f4 00A73A38 kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   255 00f4 00A73A53 kernel32.GetProcAddress(7beb0000, 00d20698 "EncodePointer") -> 7bf45e50
   256 00f4 00A73A5D ntdll.RtlEncodePointer(7bebcb64) -> 190acacb
   257 00f4 00A73A38 kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   258 00f4 00A73A53 kernel32.GetProcAddress(7beb0000, 00d20698 "EncodePointer") -> 7bf45e50
   259 00f4 00A73A5D ntdll.RtlEncodePointer(7beb4590) -> 5e70cacb
   260 00f4 00A73AAF kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   261 00f4 00A73ACA kernel32.GetProcAddress(7beb0000, 00d206b8 "DecodePointer") -> 7bf45910
   262 00f4 00A73AD4 ntdll.RtlDecodePointer(fcb8f73e) -> 00000000
   263 00f4 00A80112 kernel32.GetModuleHandleA(00d260f0 "kernel32.dll") -> 7beb0000
   264 00f4 00A80122 kernel32.GetProcAddress(7beb0000, 00d260c8 "InitializeCriticalSectionAndSpinCount") -> 7beb7650
   265 00f4 00A73A38 kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   266 00f4 00A73A53 kernel32.GetProcAddress(7beb0000, 00d20698 "EncodePointer") -> 7bf45e50
   267 00f4 00A73A5D ntdll.RtlEncodePointer(7beb7650) -> 4790cacb
   268 00f4 00A80144 kernel32.InitializeCriticalSectionAndSpinCount(010db490, 00000fa0) -> 00000001
   269 00f4 00A73AAF kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   270 00f4 00A73ACA kernel32.GetProcAddress(7beb0000, 00d206b8 "DecodePointer") -> 7bf45910
   271 00f4 00A73AD4 ntdll.RtlDecodePointer(4790cacb) -> 7beb7650
   272 00f4 00A80144 kernel32.InitializeCriticalSectionAndSpinCount(010db4a8, 00000fa0) -> 00000001
   273 00f4 00A73AAF kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   274 00f4 00A73ACA kernel32.GetProcAddress(7beb0000, 00d206b8 "DecodePointer") -> 7bf45910
   275 00f4 00A73AD4 ntdll.RtlDecodePointer(4790cacb) -> 7beb7650
   276 00f4 00A80144 kernel32.InitializeCriticalSectionAndSpinCount(010db4c0, 00000fa0) -> 00000001
   277 00f4 00A73AAF kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   278 00f4 00A73ACA kernel32.GetProcAddress(7beb0000, 00d206b8 "DecodePointer") -> 7bf45910
   279 00f4 00A73AD4 ntdll.RtlDecodePointer(4790cacb) -> 7beb7650
   280 00f4 00A80144 kernel32.InitializeCriticalSectionAndSpinCount(010db4d8, 00000fa0) -> 00000001
   281 00f4 00A73AAF kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   282 00f4 00A73ACA kernel32.GetProcAddress(7beb0000, 00d206b8 "DecodePointer") -> 7bf45910
   283 00f4 00A73AD4 ntdll.RtlDecodePointer(4790cacb) -> 7beb7650
   284 00f4 00A80144 kernel32.InitializeCriticalSectionAndSpinCount(010db4f0, 00000fa0) -> 00000001
   285 00f4 00A73AAF kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   286 00f4 00A73ACA kernel32.GetProcAddress(7beb0000, 00d206b8 "DecodePointer") -> 7bf45910
   287 00f4 00A73AD4 ntdll.RtlDecodePointer(4790cacb) -> 7beb7650
   288 00f4 00A80144 kernel32.InitializeCriticalSectionAndSpinCount(010db508, 00000fa0) -> 00000001
   289 00f4 00A73AAF kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   290 00f4 00A73ACA kernel32.GetProcAddress(7beb0000, 00d206b8 "DecodePointer") -> 7bf45910
   291 00f4 00A73AD4 ntdll.RtlDecodePointer(4790cacb) -> 7beb7650
   292 00f4 00A80144 kernel32.InitializeCriticalSectionAndSpinCount(010db520, 00000fa0) -> 00000001
   293 00f4 00A73AAF kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   294 00f4 00A73ACA kernel32.GetProcAddress(7beb0000, 00d206b8 "DecodePointer") -> 7bf45910
   295 00f4 00A73AD4 ntdll.RtlDecodePointer(4790cacb) -> 7beb7650
   296 00f4 00A80144 kernel32.InitializeCriticalSectionAndSpinCount(010db538, 00000fa0) -> 00000001
   297 00f4 00A73AAF kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   298 00f4 00A73ACA kernel32.GetProcAddress(7beb0000, 00d206b8 "DecodePointer") -> 7bf45910
   299 00f4 00A73AD4 ntdll.RtlDecodePointer(4790cacb) -> 7beb7650
   300 00f4 00A80144 kernel32.InitializeCriticalSectionAndSpinCount(010db550, 00000fa0) -> 00000001
   301 00f4 00A73AAF kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   302 00f4 00A73ACA kernel32.GetProcAddress(7beb0000, 00d206b8 "DecodePointer") -> 7bf45910
   303 00f4 00A73AD4 ntdll.RtlDecodePointer(4790cacb) -> 7beb7650
   304 00f4 00A80144 kernel32.InitializeCriticalSectionAndSpinCount(010db568, 00000fa0) -> 00000001
   305 00f4 00A73AAF kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   306 00f4 00A73ACA kernel32.GetProcAddress(7beb0000, 00d206b8 "DecodePointer") -> 7bf45910
   307 00f4 00A73AD4 ntdll.RtlDecodePointer(4790cacb) -> 7beb7650
   308 00f4 00A80144 kernel32.InitializeCriticalSectionAndSpinCount(010db580, 00000fa0) -> 00000001
   309 00f4 00A73AAF kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   310 00f4 00A73ACA kernel32.GetProcAddress(7beb0000, 00d206b8 "DecodePointer") -> 7bf45910
   311 00f4 00A73AD4 ntdll.RtlDecodePointer(4790cacb) -> 7beb7650
   312 00f4 00A80144 kernel32.InitializeCriticalSectionAndSpinCount(010db598, 00000fa0) -> 00000001
   313 00f4 00A73AAF kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   314 00f4 00A73ACA kernel32.GetProcAddress(7beb0000, 00d206b8 "DecodePointer") -> 7bf45910
   315 00f4 00A73AD4 ntdll.RtlDecodePointer(4790cacb) -> 7beb7650
   316 00f4 00A80144 kernel32.InitializeCriticalSectionAndSpinCount(010db5b0, 00000fa0) -> 00000001
   317 00f4 00A73AAF kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   318 00f4 00A73ACA kernel32.GetProcAddress(7beb0000, 00d206b8 "DecodePointer") -> 7bf45910
   319 00f4 00A73AD4 ntdll.RtlDecodePointer(4790cacb) -> 7beb7650
   320 00f4 00A80144 kernel32.InitializeCriticalSectionAndSpinCount(010db5c8, 00000fa0) -> 00000001
   321 00f4 00A73AAF kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   322 00f4 00A73ACA kernel32.GetProcAddress(7beb0000, 00d206b8 "DecodePointer") -> 7bf45910
   323 00f4 00A73AD4 ntdll.RtlDecodePointer(5e00cacb) -> 7beb4570
   324 00f4 00A73F96 kernel32.FlsAlloc(00a73cd8) -> 00000001
   325 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000214) -> 024a0488
   326 00f4 00A73AAF kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   327 00f4 00A73ACA kernel32.GetProcAddress(7beb0000, 00d206b8 "DecodePointer") -> 7bf45910
   328 00f4 00A73AD4 ntdll.RtlDecodePointer(190acacb) -> 7bebcb64
   329 00f4 00A73BA1 kernel32.GetModuleHandleA(00d206a8 "KERNEL32.DLL") -> 7beb0000
   330 00f4 00A73BD1 kernel32.GetProcAddress(7beb0000, 00d20698 "EncodePointer") -> 7bf45e50
   331 00f4 00A73BE1 kernel32.GetProcAddress(7beb0000, 00d206b8 "DecodePointer") -> 7bf45910
   332 00f4 00A73C07 kernel32.InterlockedIncrement(00ee9758) -> 00000001
   333 00f4 00A71C52 kernel32.InterlockedIncrement(00ee9c88) -> 00000002
   334 00f4 00A71CC2 kernel32.InterlockedIncrement(00ee9f34) -> 00000001
   335 00f4 00A75C5A kernel32.GetStartupInfoA(0026fe2c) -> 7bec40c0
   336 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000800) -> 024a06a8
   337 00f4 00A75E0E kernel32.GetStdHandle(fffffff6) -> 00000008
   338 00f4 00A75E20 kernel32.GetFileType(00000008) -> 00000002
   339 00f4 00A73AD4 ntdll.RtlDecodePointer(4790cacb) -> 7beb7650
   340 00f4 00A80144 kernel32.InitializeCriticalSectionAndSpinCount(024a06b4, 00000fa0) -> 00000001
   341 00f4 00A75E0E kernel32.GetStdHandle(fffffff5) -> 0000000c
   342 00f4 00A75E20 kernel32.GetFileType(0000000c) -> 00000001
   343 00f4 00A73AD4 ntdll.RtlDecodePointer(4790cacb) -> 7beb7650
   344 00f4 00A80144 kernel32.InitializeCriticalSectionAndSpinCount(024a06f4, 00000fa0) -> 00000001
   345 00f4 00A75E0E kernel32.GetStdHandle(fffffff4) -> 00000010
   346 00f4 00A75E20 kernel32.GetFileType(00000010) -> 00000001
   347 00f4 00A73AD4 ntdll.RtlDecodePointer(4790cacb) -> 7beb7650
   348 00f4 00A80144 kernel32.InitializeCriticalSectionAndSpinCount(024a0734, 00000fa0) -> 00000001
   349 00f4 00A75E78 kernel32.SetHandleCount(00000020) -> 00000020
   350 00f4 00A692C6 kernel32.GetCommandLineA() -> 013d8348
   351 00f4 00A76F70 kernel32.GetEnvironmentStringsW() -> 01403fa8
   352 00f4 00A76FE5 kernel32.WideCharToMultiByte(00000000, 00000000, 01403fa8 L"TERM_SESSION_ID=w0t0p0:49AD342F-1C5C-4384-869B-30, 000016ab, 00000000, 00000000, 00000000, 00000000) -> 000016ab
   353 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 000016ab) -> 024a0eb0
   354 00f4 00A77007 kernel32.WideCharToMultiByte(00000000, 00000000, 01403fa8 L"TERM_SESSION_ID=w0t0p0:49AD342F-1C5C-4384-869B-30, 000016ab, 024a0eb0, 000016ab, 00000000, 00000000) -> 000016ab
   355 00f4 00A77024 kernel32.FreeEnvironmentStringsW(01403fa8) -> 00000001
   356 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   357 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   358 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   359 00f4 00A80740 kernel32.GetACP() -> 000004e4
   360 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000220) -> 024a2568
   361 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   362 00f4 00A807E1 kernel32.IsValidCodePage(000004e4) -> 00000001
   363 00f4 00A807F4 kernel32.GetCPInfo(000004e4, 0026fe0c) -> 00000001
   364 00f4 00A804EC kernel32.GetCPInfo(000004e4, 0026f8d8) -> 00000001
   365 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   366 00f4 00A82436 kernel32.GetStringTypeW(00000001, 00d1fd30 L"", 00000001, 0026f868) -> 00000001
   367 00f4 00A824A9 kernel32.MultiByteToWideChar(000004e4, 00000001, 0026fcec " \x01\x02\x03\x04\x05\x06\x07\x08\t\n\x0b\x0c\r\x0, 00000100, 00000000, 00000000) -> 00000100
   368 00f4 00A82513 kernel32.MultiByteToWideChar(000004e4, 00000001, 0026fcec " \x01\x02\x03\x04\x05\x06\x07\x08\t\n\x0b\x0c\r\x0, 00000100, 0026f658, 00000100) -> 00000100
   369 00f4 00A82525 kernel32.GetStringTypeW(00000001, 0026f658 L" \0001\0002\0003\0004\0005\0006\0007\0008\t\n\000, 00000100, 0026f8ec) -> 00000001
   370 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   371 00f4 00A70B8D kernel32.LCMapStringW(00000000, 00000100, 00d1fd30 L"", 00000001, 00000000, 00000000) -> 00000001
   372 00f4 00A70C27 kernel32.MultiByteToWideChar(000004e4, 00000001, 0026fcec " \x01\x02\x03\x04\x05\x06\x07\x08\t\n\x0b\x0c\r\x0, 00000100, 00000000, 00000000) -> 00000100
   373 00f4 00A70C93 kernel32.MultiByteToWideChar(000004e4, 00000001, 0026fcec " \x01\x02\x03\x04\x05\x06\x07\x08\t\n\x0b\x0c\r\x0, 00000100, 0026f628, 00000100) -> 00000100
   374 00f4 00A70CAF kernel32.LCMapStringW(00000000, 00000100, 0026f628 L" \0001\0002\0003\0004\0005\0006\0007\0008\t\n\000, 00000100, 00000000, 00000000) -> 00000100
   375 00f4 00A70D50 kernel32.LCMapStringW(00000000, 00000100, 0026f628 L" \0001\0002\0003\0004\0005\0006\0007\0008\t\n\000, 00000100, 0026f418, 00000100) -> 00000100
   376 00f4 00A70D73 kernel32.WideCharToMultiByte(000004e4, 00000000, 0026f418 L" \0001\0002\0003\0004\0005\0006\0007\0008\t\n\000, 00000100, 0026fbec, 00000100, 00000000, 00000000) -> 00000100
   377 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   378 00f4 00A70C27 kernel32.MultiByteToWideChar(000004e4, 00000001, 0026fcec " \x01\x02\x03\x04\x05\x06\x07\x08\t\n\x0b\x0c\r\x0, 00000100, 00000000, 00000000) -> 00000100
   379 00f4 00A70C93 kernel32.MultiByteToWideChar(000004e4, 00000001, 0026fcec " \x01\x02\x03\x04\x05\x06\x07\x08\t\n\x0b\x0c\r\x0, 00000100, 0026f648, 00000100) -> 00000100
   380 00f4 00A70CAF kernel32.LCMapStringW(00000000, 00000200, 0026f648 L" \0001\0002\0003\0004\0005\0006\0007\0008\t\n\000, 00000100, 00000000, 00000000) -> 00000100
   381 00f4 00A70D50 kernel32.LCMapStringW(00000000, 00000200, 0026f648 L" \0001\0002\0003\0004\0005\0006\0007\0008\t\n\000, 00000100, 0026f438, 00000100) -> 00000100
   382 00f4 00A70D73 kernel32.WideCharToMultiByte(000004e4, 00000000, 0026f438 L" \0001\0002\0003\0004\0005\0006\0007\0008\t\n\000, 00000100, 0026faec, 00000100, 00000000, 00000000) -> 00000100
   383 00f4 00A809FB kernel32.InterlockedDecrement(00ee9758) -> 00000000
   384 00f4 00A80A1C kernel32.InterlockedIncrement(024a2568) -> 00000001
   385 00f4 00A80AB2 kernel32.InterlockedDecrement(00ee9758) -> ffffffff
   386 00f4 00A80AD2 kernel32.InterlockedIncrement(024a2568) -> 00000002
   387 00f4 00A76EC9 kernel32.GetModuleFileNameA(00000000, 010db378, 00000104) -> 00000045
   388 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   389 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   390 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   391 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   392 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   393 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   394 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   395 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   396 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   397 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   398 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   399 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   400 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   401 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   402 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   403 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   404 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   405 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   406 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   407 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   408 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   409 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   410 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   411 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   412 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   413 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   414 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   415 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   416 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   417 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   418 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   419 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   420 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   421 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   422 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   423 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   424 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   425 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   426 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   427 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   428 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   429 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   430 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   431 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   432 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   433 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   434 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   435 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   436 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   437 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   438 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   439 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   440 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   441 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   442 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   443 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   444 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   445 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   446 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   447 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   448 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   449 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   450 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   451 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   452 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   453 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   454 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   455 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   456 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   457 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   458 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   459 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   460 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   461 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   462 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   463 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   464 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   465 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   466 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   467 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 0000005c) -> 024a2790
   468 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   469 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   470 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   471 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   472 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   473 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   474 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   475 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   476 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   477 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   478 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   479 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   480 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   481 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   482 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   483 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   484 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   485 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   486 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   487 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   488 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   489 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   490 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   491 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   492 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   493 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   494 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   495 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   496 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   497 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   498 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   499 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   500 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   501 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   502 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   503 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   504 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   505 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   506 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   507 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   508 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   509 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   510 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   511 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   512 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   513 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   514 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   515 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   516 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   517 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   518 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   519 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   520 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   521 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   522 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   523 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   524 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   525 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   526 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   527 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   528 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   529 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   530 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   531 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   532 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   533 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   534 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   535 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   536 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   537 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   538 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   539 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   540 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   541 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   542 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   543 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   544 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   545 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   546 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   547 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 000001ec) -> 024a27f8
   548 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000003c) -> 024a29f0
   549 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000003e) -> 024a2a38
   550 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000001a) -> 024a2a80
   551 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000014) -> 024a2aa8
   552 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000012) -> 024a2ac8
   553 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000000e) -> 024a2ae8
   554 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000011) -> 024a2b00
   555 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000003e) -> 024a2b20
   556 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000000f) -> 024a2b68
   557 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000018) -> 024a2b80
   558 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000002d) -> 024a2ba0
   559 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000378) -> 024a2bd8
   560 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000014) -> 024a2f58
   561 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000016) -> 024a2f78
   562 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000014) -> 024a2f98
   563 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000056) -> 024a2fb8
   564 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000021) -> 024a3018
   565 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000043) -> 024a3048
   566 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000010) -> 024a3098
   567 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000013) -> 024a30b0
   568 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000013) -> 024a30d0
   569 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000036) -> 024a30f0
   570 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000026) -> 024a3130
   571 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000008) -> 024a3160
   572 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000003c) -> 024a3178
   573 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000001e) -> 024a31c0
   574 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000025) -> 024a31e8
   575 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000022) -> 024a3218
   576 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000192) -> 024a3248
   577 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000023) -> 024a33e8
   578 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000021) -> 024a3418
   579 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000000b) -> 024a3448
   580 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000008) -> 024a3460
   581 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000020) -> 024a3478
   582 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000060) -> 024a34a0
   583 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000031) -> 024a3508
   584 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000007) -> 024a3548
   585 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000000b) -> 024a3560
   586 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000039) -> 024a3578
   587 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000000e) -> 024a35d8
   588 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000002a) -> 024a39b8
   589 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000017) -> 024a35f8
   590 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000001e) -> 024a39f0
   591 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000023) -> 024a3a18
   592 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000042) -> 024a3a48
   593 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000001c) -> 024a3a98
   594 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000001a) -> 024a3ac0
   595 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000001b) -> 024a3ae8
   596 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000025) -> 024a3b10
   597 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000001b) -> 024a3b40
   598 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000023) -> 024a3b68
   599 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000000f) -> 024a3618
   600 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000017) -> 024a3638
   601 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000027) -> 024a3bb0
   602 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000020) -> 024a3be0
   603 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000027) -> 024a3c10
   604 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000023) -> 024a3c40
   605 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000037) -> 024a4180
   606 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000001b) -> 024a3c70
   607 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000019) -> 024a3ca0
   608 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000035) -> 024a41c0
   609 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000003d) -> 024a4200
   610 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000003f) -> 024a4248
   611 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000010) -> 024a3658
   612 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000004c) -> 024a4290
   613 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000000d) -> 024a3678
   614 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000003c) -> 024a42e8
   615 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000001c) -> 024a3cd0
   616 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000001f) -> 024a3d00
   617 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000010) -> 024a3698
   618 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000013) -> 024a36b8
   619 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000040) -> 024a4330
   620 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000080) -> 024a4378
   621 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000004d) -> 024a4400
   622 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000003d) -> 024a4458
   623 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000003c) -> 024a44a0
   624 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000016) -> 024a36d8
   625 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000024) -> 024a3d30
   626 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000018) -> 024a36f8
   627 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000000e) -> 024a3718
   628 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000068) -> 024a44e8
   629 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000039) -> 024a4558
   630 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000001b) -> 024a3d60
   631 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000048) -> 024a45a0
   632 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000012) -> 024a3738
   633 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000018) -> 024a3758
   634 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000012) -> 024a3778
   635 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000001e) -> 024a3d90
   636 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000002c) -> 024a45f0
   637 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000002b) -> 024a4628
   638 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000002c) -> 024a4660
   639 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000013) -> 024a3798
   640 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000000d) -> 024a37b8
   641 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000001b) -> 024a3dc0
   642 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000002f) -> 024a4698
   643 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000018) -> 024a37d8
   644 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000014) -> 024a37f8
   645 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000015) -> 024a3818
   646 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000014) -> 024a3838
   647 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000020) -> 024a3df0
   648 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000001f) -> 024a3e20
   649 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000001b) -> 024a3e50
   650 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000017) -> 024a3858
   651 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000029) -> 024a46d0
   652 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000001e) -> 024a3e80
   653 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000003c) -> 024a4708
   654 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000031) -> 024a4750
   655 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000017) -> 024a3878
   656 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000024) -> 024a3eb0
   657 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000037) -> 024a4790
   658 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000062) -> 024a47d0
   659 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000024) -> 024a3ee0
   660 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000046) -> 024a4840
   661 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000070) -> 024a4890
   662 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000060) -> 024a4908
   663 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000018) -> 024a3898
   664 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000015) -> 024a38b8
   665 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000000f) -> 024a38d8
   666 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000016) -> 024a38f8
   667 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 0000001d) -> 024a3f10
   668 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000012) -> 024a3918
   669 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000019) -> 024a3f40
   670 00f4 00A6A750 kernel32.HeapFree(024a0000, 00000000, 024a0eb0) -> 024b0001
   671 00f4 00A738FC kernel32.GetModuleHandleA(00d20684 "KERNEL32") -> 7beb0000
   672 00f4 00A7390C kernel32.GetProcAddress(7beb0000, 00d20668 "IsProcessorFeaturePresent") -> 7beb7950
   673 00f4 00A73914 kernel32.IsProcessorFeaturePresent(00000000) -> 00000000
   674 00f4 00A73A5D ntdll.RtlEncodePointer(00a7384c) -> 609ef76d
   675 00f4 00A73A5D ntdll.RtlEncodePointer(00a72ef1) -> 6bc0776d
   676 00f4 00A73A5D ntdll.RtlEncodePointer(00a72eaf) -> 6bef776d
   677 00f4 00A73A5D ntdll.RtlEncodePointer(00a72ee3) -> 6bc9776d
   678 00f4 00A73A5D ntdll.RtlEncodePointer(00a72e59) -> 6b94776d
   679 00f4 00A73A5D ntdll.RtlEncodePointer(00a7384c) -> 609ef76d
   680 00f4 00A73A5D ntdll.RtlEncodePointer(00a737c6) -> 675bf76d
   681 00f4 00A73A5D ntdll.RtlEncodePointer(00a72e6f) -> 6b8f776d
   682 00f4 00A73A5D ntdll.RtlEncodePointer(00a72dd9) -> 6a54776d
   683 00f4 00A73A5D ntdll.RtlEncodePointer(00a72d68) -> 6a0cf76d
   684 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000080) -> 024a0eb0
   685 00f4 00A73A5D ntdll.RtlEncodePointer(024a0eb0) -> fbe0f61b
   686 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000800) -> 024a0f38
   687 00f4 00A76A10 kernel32.SetUnhandledExceptionFilter(00a769c8) -> 00000000
   688 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   689 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   690 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   691 00f4 00A73A5D ntdll.RtlEncodePointer(00a770ab) -> 44ed776d
   692 00f4 00A73A5D ntdll.RtlEncodePointer(024a0eb4) -> fbe2f61b
   693 00f4 00A6729A kernel32.InterlockedIncrement(00ee8b8c) -> 00000000
   694 00f4 00A67100 ntdll.RtlInitializeCriticalSection(010dae80) -> 00000000
   695 00f4 00A67100 ntdll.RtlInitializeCriticalSection(010dae98) -> 00000000
   696 00f4 00A67100 ntdll.RtlInitializeCriticalSection(010daeb0) -> 00000000
   697 00f4 00A67100 ntdll.RtlInitializeCriticalSection(010daec8) -> 00000000
   698 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   699 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe2f61b) -> 024a0eb4
   700 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   701 00f4 00A73A5D ntdll.RtlEncodePointer(00c4156a) -> f60df75c
   702 00f4 00A73A5D ntdll.RtlEncodePointer(024a0eb8) -> fbe4f61b
   703 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000018) -> 024a3938
   704 00f4 00A67100 ntdll.RtlInitializeCriticalSection(024a3938) -> 00000000
   705 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000004) -> 024a3958
   706 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000034) -> 024a1740
   707 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   708 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe4f61b) -> 024a0eb8
   709 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   710 00f4 00A73A5D ntdll.RtlEncodePointer(00c4157e) -> f607f75c
   711 00f4 00A73A5D ntdll.RtlEncodePointer(024a0ebc) -> fbe6f61b
   712 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000004) -> 024a3978
   713 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000018) -> 024a3998
   714 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   715 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   716 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 000000d8) -> 024a1780
   717 00f4 00A71C52 kernel32.InterlockedIncrement(024a1780) -> 00000001
   718 00f4 00A71CC2 kernel32.InterlockedIncrement(00ee9f34) -> 00000002
   719 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000355) -> 024a1860
   720 00f4 00A6A750 kernel32.HeapFree(024a0000, 00000000, 024a1860) -> 024b0001
   721 00f4 00A71C52 kernel32.InterlockedIncrement(024a1780) -> 00000002
   722 00f4 00A71CC2 kernel32.InterlockedIncrement(00ee9f34) -> 00000003
   723 00f4 00A71CDC kernel32.InterlockedDecrement(00ee9c88) -> 00000001
   724 00f4 00A71D4C kernel32.InterlockedDecrement(00ee9f34) -> 00000002
   725 00f4 00A71CDC kernel32.InterlockedDecrement(024a1780) -> 00000001
   726 00f4 00A71D4C kernel32.InterlockedDecrement(00ee9f34) -> 00000001
   727 00f4 00A71C52 kernel32.InterlockedIncrement(024a1780) -> 00000002
   728 00f4 00A71CC2 kernel32.InterlockedIncrement(00ee9f34) -> 00000002
   729 00f4 00A71CDC kernel32.InterlockedDecrement(00ee9c88) -> 00000000
   730 00f4 00A71D4C kernel32.InterlockedDecrement(00ee9f34) -> 00000001
   731 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   732 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   733 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 000000d8) -> 024a1860
   734 00f4 00A71C52 kernel32.InterlockedIncrement(024a1860) -> 00000001
   735 00f4 00A71CC2 kernel32.InterlockedIncrement(00ee9f34) -> 00000002
   736 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   737 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000355) -> 024a1940
   738 00f4 00A6A750 kernel32.HeapFree(024a0000, 00000000, 024a1940) -> 024b0001
   739 00f4 00A71C52 kernel32.InterlockedIncrement(024a1860) -> 00000002
   740 00f4 00A71CC2 kernel32.InterlockedIncrement(00ee9f34) -> 00000003
   741 00f4 00A71CDC kernel32.InterlockedDecrement(024a1780) -> 00000001
   742 00f4 00A71D4C kernel32.InterlockedDecrement(00ee9f34) -> 00000002
   743 00f4 00A71CDC kernel32.InterlockedDecrement(024a1860) -> 00000001
   744 00f4 00A71D4C kernel32.InterlockedDecrement(00ee9f34) -> 00000001
   745 00f4 00A71C52 kernel32.InterlockedIncrement(024a1860) -> 00000002
   746 00f4 00A71CC2 kernel32.InterlockedIncrement(00ee9f34) -> 00000002
   747 00f4 00A71CDC kernel32.InterlockedDecrement(024a1780) -> 00000000
   748 00f4 00A71D4C kernel32.InterlockedDecrement(00ee9f34) -> 00000001
   749 00f4 00A6A750 kernel32.HeapFree(024a0000, 00000000, 024a1780) -> 024b0001
   750 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   751 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   752 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 00000200) -> 024a1940
   753 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   754 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   755 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   756 00f4 00A6E250 ntdll.RtlAllocateHeap(024a0000, 00000008, 000000d8) -> 024a1780
   757 00f4 00A71C52 kernel32.InterlockedIncrement(024a1780) -> 00000001
   758 00f4 00A71CC2 kernel32.InterlockedIncrement(00ee9f34) -> 00000002
   759 00f4 00A73C51 kernel32.GetLastError() -> 00000000
   760 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000355) -> 024a1b48
   761 00f4 00A6A750 kernel32.HeapFree(024a0000, 00000000, 024a1b48) -> 024b0001
   762 00f4 00A71C52 kernel32.InterlockedIncrement(024a1780) -> 00000002
   763 00f4 00A71CC2 kernel32.InterlockedIncrement(00ee9f34) -> 00000003
   764 00f4 00A71CDC kernel32.InterlockedDecrement(024a1860) -> 00000001
   765 00f4 00A71D4C kernel32.InterlockedDecrement(00ee9f34) -> 00000002
   766 00f4 00A71CDC kernel32.InterlockedDecrement(024a1780) -> 00000001
   767 00f4 00A71D4C kernel32.InterlockedDecrement(00ee9f34) -> 00000001
   768 00f4 00A71C52 kernel32.InterlockedIncrement(024a1780) -> 00000002
   769 00f4 00A71CC2 kernel32.InterlockedIncrement(00ee9f34) -> 00000002
   770 00f4 00A71CDC kernel32.InterlockedDecrement(024a1860) -> 00000000
   771 00f4 00A71D4C kernel32.InterlockedDecrement(00ee9f34) -> 00000001
   772 00f4 00A6A750 kernel32.HeapFree(024a0000, 00000000, 024a1860) -> 024b0001
   773 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000008) -> 024a1b60
   774 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   775 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe6f61b) -> 024a0ebc
   776 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   777 00f4 00A73A5D ntdll.RtlEncodePointer(00c41574) -> f602f75c
   778 00f4 00A73A5D ntdll.RtlEncodePointer(024a0ec0) -> fbd8f61b
   779 00f4 00A6729A kernel32.InterlockedIncrement(00ee8b8c) -> 00000001
   780 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   781 00f4 00A73AD4 ntdll.RtlDecodePointer(fbd8f61b) -> 024a0ec0
   782 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   783 00f4 00A73A5D ntdll.RtlEncodePointer(00c41588) -> f67cf75c
   784 00f4 00A73A5D ntdll.RtlEncodePointer(024a0ec4) -> fbdaf61b
   785 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000018) -> 024a1b80
   786 00f4 00A67100 ntdll.RtlInitializeCriticalSection(024a1b80) -> 00000000
   787 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000004) -> 024a1ba0
   788 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   789 00f4 00A73AD4 ntdll.RtlDecodePointer(fbdaf61b) -> 024a0ec4
   790 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   791 00f4 00A73A5D ntdll.RtlEncodePointer(00c4159c) -> f676f75c
   792 00f4 00A73A5D ntdll.RtlEncodePointer(024a0ec8) -> fbdcf61b
   793 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000004) -> 024a1bc0
   794 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   795 00f4 00A73AD4 ntdll.RtlDecodePointer(fbdcf61b) -> 024a0ec8
   796 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   797 00f4 00A73A5D ntdll.RtlEncodePointer(00c41592) -> f671f75c
   798 00f4 00A73A5D ntdll.RtlEncodePointer(024a0ecc) -> fbdef61b
   799 00f4 00A6729A kernel32.InterlockedIncrement(00ee8b8c) -> 00000002
   800 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   801 00f4 00A73AD4 ntdll.RtlDecodePointer(fbdef61b) -> 024a0ecc
   802 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   803 00f4 00A73A5D ntdll.RtlEncodePointer(00c415a6) -> f66bf75c
   804 00f4 00A73A5D ntdll.RtlEncodePointer(024a0ed0) -> fbd0f61b
   805 00f4 00A6729A kernel32.InterlockedIncrement(00ee8b8c) -> 00000003
   806 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   807 00f4 00A73AD4 ntdll.RtlDecodePointer(fbd0f61b) -> 024a0ed0
   808 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   809 00f4 00A73A5D ntdll.RtlEncodePointer(00c415ba) -> f665f75c
   810 00f4 00A73A5D ntdll.RtlEncodePointer(024a0ed4) -> fbd2f61b
   811 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   812 00f4 00A73AD4 ntdll.RtlDecodePointer(fbd2f61b) -> 024a0ed4
   813 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   814 00f4 00A73A5D ntdll.RtlEncodePointer(00c415c4) -> f65af75c
   815 00f4 00A73A5D ntdll.RtlEncodePointer(024a0ed8) -> fbd4f61b
   816 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   817 00f4 00A73AD4 ntdll.RtlDecodePointer(fbd4f61b) -> 024a0ed8
   818 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   819 00f4 00A73A5D ntdll.RtlEncodePointer(00c40780) -> ff78f75c
   820 00f4 00A73A5D ntdll.RtlEncodePointer(024a0edc) -> fbd6f61b
   821 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   822 00f4 00A73AD4 ntdll.RtlDecodePointer(fbd6f61b) -> 024a0edc
   823 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   824 00f4 00A73A5D ntdll.RtlEncodePointer(00c40790) -> ff70f75c
   825 00f4 00A73A5D ntdll.RtlEncodePointer(024a0ee0) -> fbc8f61b
   826 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   827 00f4 00A73AD4 ntdll.RtlDecodePointer(fbc8f61b) -> 024a0ee0
   828 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   829 00f4 00A73A5D ntdll.RtlEncodePointer(00c407a0) -> ff68f75c
   830 00f4 00A73A5D ntdll.RtlEncodePointer(024a0ee4) -> fbcaf61b
   831 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   832 00f4 00A73AD4 ntdll.RtlDecodePointer(fbcaf61b) -> 024a0ee4
   833 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   834 00f4 00A73A5D ntdll.RtlEncodePointer(00c407b0) -> ff60f75c
   835 00f4 00A73A5D ntdll.RtlEncodePointer(024a0ee8) -> fbccf61b
   836 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   837 00f4 00A73AD4 ntdll.RtlDecodePointer(fbccf61b) -> 024a0ee8
   838 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   839 00f4 00A73A5D ntdll.RtlEncodePointer(00c407c0) -> ff58f75c
   840 00f4 00A73A5D ntdll.RtlEncodePointer(024a0eec) -> fbcef61b
   841 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   842 00f4 00A73AD4 ntdll.RtlDecodePointer(fbcef61b) -> 024a0eec
   843 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   844 00f4 00A73A5D ntdll.RtlEncodePointer(00c407d0) -> ff50f75c
   845 00f4 00A73A5D ntdll.RtlEncodePointer(024a0ef0) -> fbc0f61b
   846 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   847 00f4 00A73AD4 ntdll.RtlDecodePointer(fbc0f61b) -> 024a0ef0
   848 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   849 00f4 00A73A5D ntdll.RtlEncodePointer(00c407f0) -> ff40f75c
   850 00f4 00A73A5D ntdll.RtlEncodePointer(024a0ef4) -> fbc2f61b
   851 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   852 00f4 00A73AD4 ntdll.RtlDecodePointer(fbc2f61b) -> 024a0ef4
   853 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   854 00f4 00A73A5D ntdll.RtlEncodePointer(00c407e0) -> ff48f75c
   855 00f4 00A73A5D ntdll.RtlEncodePointer(024a0ef8) -> fbc4f61b
   856 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   857 00f4 00A73AD4 ntdll.RtlDecodePointer(fbc4f61b) -> 024a0ef8
   858 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   859 00f4 00A73A5D ntdll.RtlEncodePointer(00c40850) -> f890f75c
   860 00f4 00A73A5D ntdll.RtlEncodePointer(024a0efc) -> fbc6f61b
   861 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   862 00f4 00A73AD4 ntdll.RtlDecodePointer(fbc6f61b) -> 024a0efc
   863 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   864 00f4 00A73A5D ntdll.RtlEncodePointer(00c40cb0) -> fae0f75c
   865 00f4 00A73A5D ntdll.RtlEncodePointer(024a0f00) -> fb38f61b
   866 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   867 00f4 00A73AD4 ntdll.RtlDecodePointer(fb38f61b) -> 024a0f00
   868 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   869 00f4 00A73A5D ntdll.RtlEncodePointer(00c408b0) -> f8e0f75c
   870 00f4 00A73A5D ntdll.RtlEncodePointer(024a0f04) -> fb3af61b
   871 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   872 00f4 00A73AD4 ntdll.RtlDecodePointer(fb3af61b) -> 024a0f04
   873 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   874 00f4 00A73A5D ntdll.RtlEncodePointer(00c408c0) -> f8d8f75c
   875 00f4 00A73A5D ntdll.RtlEncodePointer(024a0f08) -> fb3cf61b
   876 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   877 00f4 00A73AD4 ntdll.RtlDecodePointer(fb3cf61b) -> 024a0f08
   878 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   879 00f4 00A73A5D ntdll.RtlEncodePointer(00c408d0) -> f8d0f75c
   880 00f4 00A73A5D ntdll.RtlEncodePointer(024a0f0c) -> fb3ef61b
   881 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   882 00f4 00A73AD4 ntdll.RtlDecodePointer(fb3ef61b) -> 024a0f0c
   883 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   884 00f4 00A73A5D ntdll.RtlEncodePointer(00c408e0) -> f8c8f75c
   885 00f4 00A73A5D ntdll.RtlEncodePointer(024a0f10) -> fb30f61b
   886 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   887 00f4 00A73AD4 ntdll.RtlDecodePointer(fb30f61b) -> 024a0f10
   888 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   889 00f4 00A73A5D ntdll.RtlEncodePointer(00c40da0) -> fa68f75c
   890 00f4 00A73A5D ntdll.RtlEncodePointer(024a0f14) -> fb32f61b
   891 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   892 00f4 00A73AD4 ntdll.RtlDecodePointer(fb32f61b) -> 024a0f14
   893 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   894 00f4 00A73A5D ntdll.RtlEncodePointer(00c408f0) -> f8c0f75c
   895 00f4 00A73A5D ntdll.RtlEncodePointer(024a0f18) -> fb34f61b
   896 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   897 00f4 00A73AD4 ntdll.RtlDecodePointer(fb34f61b) -> 024a0f18
   898 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   899 00f4 00A73A5D ntdll.RtlEncodePointer(00c40900) -> f838f75c
   900 00f4 00A73A5D ntdll.RtlEncodePointer(024a0f1c) -> fb36f61b
   901 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   902 00f4 00A73AD4 ntdll.RtlDecodePointer(fb36f61b) -> 024a0f1c
   903 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   904 00f4 00A73A5D ntdll.RtlEncodePointer(00c40910) -> f830f75c
   905 00f4 00A73A5D ntdll.RtlEncodePointer(024a0f20) -> fb28f61b
   906 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   907 00f4 00A73AD4 ntdll.RtlDecodePointer(fb28f61b) -> 024a0f20
   908 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   909 00f4 00A73A5D ntdll.RtlEncodePointer(00c40920) -> f828f75c
   910 00f4 00A73A5D ntdll.RtlEncodePointer(024a0f24) -> fb2af61b
   911 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   912 00f4 00A73AD4 ntdll.RtlDecodePointer(fb2af61b) -> 024a0f24
   913 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   914 00f4 00A73A5D ntdll.RtlEncodePointer(00c40db0) -> fa60f75c
   915 00f4 00A73A5D ntdll.RtlEncodePointer(024a0f28) -> fb2cf61b
   916 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   917 00f4 00A73AD4 ntdll.RtlDecodePointer(fb2cf61b) -> 024a0f28
   918 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   919 00f4 00A73A5D ntdll.RtlEncodePointer(00c40930) -> f820f75c
   920 00f4 00A73A5D ntdll.RtlEncodePointer(024a0f2c) -> fb2ef61b
   921 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   922 00f4 00A73AD4 ntdll.RtlDecodePointer(fb2ef61b) -> 024a0f2c
   923 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   924 00f4 00A73A5D ntdll.RtlEncodePointer(00c40940) -> f818f75c
   925 00f4 00A73A5D ntdll.RtlEncodePointer(024a0f30) -> fb20f61b
   926 00f4 00A73AD4 ntdll.RtlDecodePointer(fbe0f61b) -> 024a0eb0
   927 00f4 00A73AD4 ntdll.RtlDecodePointer(fb20f61b) -> 024a0f30
   928 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a0eb0) -> 00000080
   929 00f4 00A6DFBF ntdll.RtlReAllocateHeap(024a0000, 00000000, 024a0eb0, 00000100) -> 024a1f40
   930 00f4 00A73A5D ntdll.RtlEncodePointer(024a1f40) -> f318f61b
   931 00f4 00A73A5D ntdll.RtlEncodePointer(00c40950) -> f810f75c
   932 00f4 00A73A5D ntdll.RtlEncodePointer(024a1fc4) -> f35af61b
   933 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
   934 00f4 00A73AD4 ntdll.RtlDecodePointer(f35af61b) -> 024a1fc4
   935 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
   936 00f4 00A73A5D ntdll.RtlEncodePointer(00c40960) -> f808f75c
   937 00f4 00A73A5D ntdll.RtlEncodePointer(024a1fc8) -> f35cf61b
   938 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
   939 00f4 00A73AD4 ntdll.RtlDecodePointer(f35cf61b) -> 024a1fc8
   940 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
   941 00f4 00A73A5D ntdll.RtlEncodePointer(00c40970) -> f800f75c
   942 00f4 00A73A5D ntdll.RtlEncodePointer(024a1fcc) -> f35ef61b
   943 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
   944 00f4 00A73AD4 ntdll.RtlDecodePointer(f35ef61b) -> 024a1fcc
   945 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
   946 00f4 00A73A5D ntdll.RtlEncodePointer(00c40980) -> f878f75c
   947 00f4 00A73A5D ntdll.RtlEncodePointer(024a1fd0) -> f350f61b
   948 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
   949 00f4 00A73AD4 ntdll.RtlDecodePointer(f350f61b) -> 024a1fd0
   950 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
   951 00f4 00A73A5D ntdll.RtlEncodePointer(00c40990) -> f870f75c
   952 00f4 00A73A5D ntdll.RtlEncodePointer(024a1fd4) -> f352f61b
   953 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
   954 00f4 00A73AD4 ntdll.RtlDecodePointer(f352f61b) -> 024a1fd4
   955 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
   956 00f4 00A73A5D ntdll.RtlEncodePointer(00c409a0) -> f868f75c
   957 00f4 00A73A5D ntdll.RtlEncodePointer(024a1fd8) -> f354f61b
   958 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
   959 00f4 00A73AD4 ntdll.RtlDecodePointer(f354f61b) -> 024a1fd8
   960 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
   961 00f4 00A73A5D ntdll.RtlEncodePointer(00c409b0) -> f860f75c
   962 00f4 00A73A5D ntdll.RtlEncodePointer(024a1fdc) -> f356f61b
   963 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
   964 00f4 00A73AD4 ntdll.RtlDecodePointer(f356f61b) -> 024a1fdc
   965 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
   966 00f4 00A73A5D ntdll.RtlEncodePointer(00c409c0) -> f858f75c
   967 00f4 00A73A5D ntdll.RtlEncodePointer(024a1fe0) -> f348f61b
   968 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
   969 00f4 00A73AD4 ntdll.RtlDecodePointer(f348f61b) -> 024a1fe0
   970 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
   971 00f4 00A73A5D ntdll.RtlEncodePointer(00c40d60) -> fa08f75c
   972 00f4 00A73A5D ntdll.RtlEncodePointer(024a1fe4) -> f34af61b
   973 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
   974 00f4 00A73AD4 ntdll.RtlDecodePointer(f34af61b) -> 024a1fe4
   975 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
   976 00f4 00A73A5D ntdll.RtlEncodePointer(00c40d00) -> fa38f75c
   977 00f4 00A73A5D ntdll.RtlEncodePointer(024a1fe8) -> f34cf61b
   978 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
   979 00f4 00A73AD4 ntdll.RtlDecodePointer(f34cf61b) -> 024a1fe8
   980 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
   981 00f4 00A73A5D ntdll.RtlEncodePointer(00c40d10) -> fa30f75c
   982 00f4 00A73A5D ntdll.RtlEncodePointer(024a1fec) -> f34ef61b
   983 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
   984 00f4 00A73AD4 ntdll.RtlDecodePointer(f34ef61b) -> 024a1fec
   985 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
   986 00f4 00A73A5D ntdll.RtlEncodePointer(00c40bf0) -> f940f75c
   987 00f4 00A73A5D ntdll.RtlEncodePointer(024a1ff0) -> f340f61b
   988 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
   989 00f4 00A73AD4 ntdll.RtlDecodePointer(f340f61b) -> 024a1ff0
   990 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
   991 00f4 00A73A5D ntdll.RtlEncodePointer(00c40d20) -> fa28f75c
   992 00f4 00A73A5D ntdll.RtlEncodePointer(024a1ff4) -> f342f61b
   993 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
   994 00f4 00A73AD4 ntdll.RtlDecodePointer(f342f61b) -> 024a1ff4
   995 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
   996 00f4 00A73A5D ntdll.RtlEncodePointer(00c40c00) -> fab8f75c
   997 00f4 00A73A5D ntdll.RtlEncodePointer(024a1ff8) -> f344f61b
   998 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
   999 00f4 00A73AD4 ntdll.RtlDecodePointer(f344f61b) -> 024a1ff8
  1000 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
  1001 00f4 00A73A5D ntdll.RtlEncodePointer(00c409d0) -> f850f75c
  1002 00f4 00A73A5D ntdll.RtlEncodePointer(024a1ffc) -> f346f61b
  1003 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1004 00f4 00A73AD4 ntdll.RtlDecodePointer(f346f61b) -> 024a1ffc
  1005 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
  1006 00f4 00A73A5D ntdll.RtlEncodePointer(00c409e0) -> f848f75c
  1007 00f4 00A73A5D ntdll.RtlEncodePointer(024a2000) -> ecb8f61b
  1008 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1009 00f4 00A73AD4 ntdll.RtlDecodePointer(ecb8f61b) -> 024a2000
  1010 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
  1011 00f4 00A73A5D ntdll.RtlEncodePointer(00c409f0) -> f840f75c
  1012 00f4 00A73A5D ntdll.RtlEncodePointer(024a2004) -> ecbaf61b
  1013 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1014 00f4 00A73AD4 ntdll.RtlDecodePointer(ecbaf61b) -> 024a2004
  1015 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
  1016 00f4 00A73A5D ntdll.RtlEncodePointer(00c40de0) -> fa48f75c
  1017 00f4 00A73A5D ntdll.RtlEncodePointer(024a2008) -> ecbcf61b
  1018 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1019 00f4 00A73AD4 ntdll.RtlDecodePointer(ecbcf61b) -> 024a2008
  1020 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
  1021 00f4 00A73A5D ntdll.RtlEncodePointer(00c40d70) -> fa00f75c
  1022 00f4 00A73A5D ntdll.RtlEncodePointer(024a200c) -> ecbef61b
  1023 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1024 00f4 00A73AD4 ntdll.RtlDecodePointer(ecbef61b) -> 024a200c
  1025 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
  1026 00f4 00A73A5D ntdll.RtlEncodePointer(00c40d30) -> fa20f75c
  1027 00f4 00A73A5D ntdll.RtlEncodePointer(024a2010) -> ecb0f61b
  1028 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1029 00f4 00A73AD4 ntdll.RtlDecodePointer(ecb0f61b) -> 024a2010
  1030 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
  1031 00f4 00A73A5D ntdll.RtlEncodePointer(00c40a00) -> f9b8f75c
  1032 00f4 00A73A5D ntdll.RtlEncodePointer(024a2014) -> ecb2f61b
  1033 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1034 00f4 00A73AD4 ntdll.RtlDecodePointer(ecb2f61b) -> 024a2014
  1035 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
  1036 00f4 00A73A5D ntdll.RtlEncodePointer(00c40dc0) -> fa58f75c
  1037 00f4 00A73A5D ntdll.RtlEncodePointer(024a2018) -> ecb4f61b
  1038 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1039 00f4 00A73AD4 ntdll.RtlDecodePointer(ecb4f61b) -> 024a2018
  1040 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
  1041 00f4 00A73A5D ntdll.RtlEncodePointer(00c40a10) -> f9b0f75c
  1042 00f4 00A73A5D ntdll.RtlEncodePointer(024a201c) -> ecb6f61b
  1043 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1044 00f4 00A73AD4 ntdll.RtlDecodePointer(ecb6f61b) -> 024a201c
  1045 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
  1046 00f4 00A73A5D ntdll.RtlEncodePointer(00c40a20) -> f9a8f75c
  1047 00f4 00A73A5D ntdll.RtlEncodePointer(024a2020) -> eca8f61b
  1048 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1049 00f4 00A73AD4 ntdll.RtlDecodePointer(eca8f61b) -> 024a2020
  1050 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
  1051 00f4 00A73A5D ntdll.RtlEncodePointer(00c40a30) -> f9a0f75c
  1052 00f4 00A73A5D ntdll.RtlEncodePointer(024a2024) -> ecaaf61b
  1053 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1054 00f4 00A73AD4 ntdll.RtlDecodePointer(ecaaf61b) -> 024a2024
  1055 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
  1056 00f4 00A73A5D ntdll.RtlEncodePointer(00c40a40) -> f998f75c
  1057 00f4 00A73A5D ntdll.RtlEncodePointer(024a2028) -> ecacf61b
  1058 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1059 00f4 00A73AD4 ntdll.RtlDecodePointer(ecacf61b) -> 024a2028
  1060 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
  1061 00f4 00A73A5D ntdll.RtlEncodePointer(00c40c20) -> faa8f75c
  1062 00f4 00A73A5D ntdll.RtlEncodePointer(024a202c) -> ecaef61b
  1063 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1064 00f4 00A73AD4 ntdll.RtlDecodePointer(ecaef61b) -> 024a202c
  1065 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
  1066 00f4 00A73A5D ntdll.RtlEncodePointer(00c40a50) -> f990f75c
  1067 00f4 00A73A5D ntdll.RtlEncodePointer(024a2030) -> eca0f61b
  1068 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1069 00f4 00A73AD4 ntdll.RtlDecodePointer(eca0f61b) -> 024a2030
  1070 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
  1071 00f4 00A73A5D ntdll.RtlEncodePointer(00c40a60) -> f988f75c
  1072 00f4 00A73A5D ntdll.RtlEncodePointer(024a2034) -> eca2f61b
  1073 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1074 00f4 00A73AD4 ntdll.RtlDecodePointer(eca2f61b) -> 024a2034
  1075 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
  1076 00f4 00A73A5D ntdll.RtlEncodePointer(00c40dd0) -> fa50f75c
  1077 00f4 00A73A5D ntdll.RtlEncodePointer(024a2038) -> eca4f61b
  1078 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1079 00f4 00A73AD4 ntdll.RtlDecodePointer(eca4f61b) -> 024a2038
  1080 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
  1081 00f4 00A73A5D ntdll.RtlEncodePointer(00c40d80) -> fa78f75c
  1082 00f4 00A73A5D ntdll.RtlEncodePointer(024a203c) -> eca6f61b
  1083 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1084 00f4 00A73AD4 ntdll.RtlDecodePointer(eca6f61b) -> 024a203c
  1085 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
  1086 00f4 00A73A5D ntdll.RtlEncodePointer(00c40c30) -> faa0f75c
  1087 00f4 00A73A5D ntdll.RtlEncodePointer(024a2040) -> ec98f61b
  1088 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1089 00f4 00A73AD4 ntdll.RtlDecodePointer(ec98f61b) -> 024a2040
  1090 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000100
  1091 00f4 00A6DFBF ntdll.RtlReAllocateHeap(024a0000, 00000000, 024a1f40, 00000200) -> 024a1f40
  1092 00f4 00A73A5D ntdll.RtlEncodePointer(024a1f40) -> f318f61b
  1093 00f4 00A73A5D ntdll.RtlEncodePointer(00c40a70) -> f980f75c
  1094 00f4 00A73A5D ntdll.RtlEncodePointer(024a2044) -> ec9af61b
  1095 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1096 00f4 00A73AD4 ntdll.RtlDecodePointer(ec9af61b) -> 024a2044
  1097 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1098 00f4 00A73A5D ntdll.RtlEncodePointer(00c40a80) -> f9f8f75c
  1099 00f4 00A73A5D ntdll.RtlEncodePointer(024a2048) -> ec9cf61b
  1100 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1101 00f4 00A73AD4 ntdll.RtlDecodePointer(ec9cf61b) -> 024a2048
  1102 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1103 00f4 00A73A5D ntdll.RtlEncodePointer(00c40a90) -> f9f0f75c
  1104 00f4 00A73A5D ntdll.RtlEncodePointer(024a204c) -> ec9ef61b
  1105 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1106 00f4 00A73AD4 ntdll.RtlDecodePointer(ec9ef61b) -> 024a204c
  1107 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1108 00f4 00A73A5D ntdll.RtlEncodePointer(00c40aa0) -> f9e8f75c
  1109 00f4 00A73A5D ntdll.RtlEncodePointer(024a2050) -> ec90f61b
  1110 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1111 00f4 00A73AD4 ntdll.RtlDecodePointer(ec90f61b) -> 024a2050
  1112 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1113 00f4 00A73A5D ntdll.RtlEncodePointer(00c40ab0) -> f9e0f75c
  1114 00f4 00A73A5D ntdll.RtlEncodePointer(024a2054) -> ec92f61b
  1115 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1116 00f4 00A73AD4 ntdll.RtlDecodePointer(ec92f61b) -> 024a2054
  1117 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1118 00f4 00A73A5D ntdll.RtlEncodePointer(00c40e00) -> fbb8f75c
  1119 00f4 00A73A5D ntdll.RtlEncodePointer(024a2058) -> ec94f61b
  1120 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1121 00f4 00A73AD4 ntdll.RtlDecodePointer(ec94f61b) -> 024a2058
  1122 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1123 00f4 00A73A5D ntdll.RtlEncodePointer(00c40ac0) -> f9d8f75c
  1124 00f4 00A73A5D ntdll.RtlEncodePointer(024a205c) -> ec96f61b
  1125 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1126 00f4 00A73AD4 ntdll.RtlDecodePointer(ec96f61b) -> 024a205c
  1127 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1128 00f4 00A73A5D ntdll.RtlEncodePointer(00c40ad0) -> f9d0f75c
  1129 00f4 00A73A5D ntdll.RtlEncodePointer(024a2060) -> ec88f61b
  1130 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1131 00f4 00A73AD4 ntdll.RtlDecodePointer(ec88f61b) -> 024a2060
  1132 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1133 00f4 00A73A5D ntdll.RtlEncodePointer(00c40ae0) -> f9c8f75c
  1134 00f4 00A73A5D ntdll.RtlEncodePointer(024a2064) -> ec8af61b
  1135 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1136 00f4 00A73AD4 ntdll.RtlDecodePointer(ec8af61b) -> 024a2064
  1137 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1138 00f4 00A73A5D ntdll.RtlEncodePointer(00c40af0) -> f9c0f75c
  1139 00f4 00A73A5D ntdll.RtlEncodePointer(024a2068) -> ec8cf61b
  1140 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1141 00f4 00A73AD4 ntdll.RtlDecodePointer(ec8cf61b) -> 024a2068
  1142 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1143 00f4 00A73A5D ntdll.RtlEncodePointer(00c40b00) -> f938f75c
  1144 00f4 00A73A5D ntdll.RtlEncodePointer(024a206c) -> ec8ef61b
  1145 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1146 00f4 00A73AD4 ntdll.RtlDecodePointer(ec8ef61b) -> 024a206c
  1147 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1148 00f4 00A73A5D ntdll.RtlEncodePointer(00c40b10) -> f930f75c
  1149 00f4 00A73A5D ntdll.RtlEncodePointer(024a2070) -> ec80f61b
  1150 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1151 00f4 00A73AD4 ntdll.RtlDecodePointer(ec80f61b) -> 024a2070
  1152 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1153 00f4 00A73A5D ntdll.RtlEncodePointer(00c40b20) -> f928f75c
  1154 00f4 00A73A5D ntdll.RtlEncodePointer(024a2074) -> ec82f61b
  1155 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1156 00f4 00A73AD4 ntdll.RtlDecodePointer(ec82f61b) -> 024a2074
  1157 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1158 00f4 00A73A5D ntdll.RtlEncodePointer(00c40b30) -> f920f75c
  1159 00f4 00A73A5D ntdll.RtlEncodePointer(024a2078) -> ec84f61b
  1160 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1161 00f4 00A73AD4 ntdll.RtlDecodePointer(ec84f61b) -> 024a2078
  1162 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1163 00f4 00A73A5D ntdll.RtlEncodePointer(00c40b40) -> f918f75c
  1164 00f4 00A73A5D ntdll.RtlEncodePointer(024a207c) -> ec86f61b
  1165 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1166 00f4 00A73AD4 ntdll.RtlDecodePointer(ec86f61b) -> 024a207c
  1167 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1168 00f4 00A73A5D ntdll.RtlEncodePointer(00c40b50) -> f910f75c
  1169 00f4 00A73A5D ntdll.RtlEncodePointer(024a2080) -> ecf8f61b
  1170 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1171 00f4 00A73AD4 ntdll.RtlDecodePointer(ecf8f61b) -> 024a2080
  1172 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1173 00f4 00A73A5D ntdll.RtlEncodePointer(00c40b60) -> f908f75c
  1174 00f4 00A73A5D ntdll.RtlEncodePointer(024a2084) -> ecfaf61b
  1175 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1176 00f4 00A73AD4 ntdll.RtlDecodePointer(ecfaf61b) -> 024a2084
  1177 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1178 00f4 00A73A5D ntdll.RtlEncodePointer(00c40cd0) -> fad0f75c
  1179 00f4 00A73A5D ntdll.RtlEncodePointer(024a2088) -> ecfcf61b
  1180 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1181 00f4 00A73AD4 ntdll.RtlDecodePointer(ecfcf61b) -> 024a2088
  1182 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1183 00f4 00A73A5D ntdll.RtlEncodePointer(00c40d90) -> fa70f75c
  1184 00f4 00A73A5D ntdll.RtlEncodePointer(024a208c) -> ecfef61b
  1185 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1186 00f4 00A73AD4 ntdll.RtlDecodePointer(ecfef61b) -> 024a208c
  1187 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1188 00f4 00A73A5D ntdll.RtlEncodePointer(00c40d40) -> fa18f75c
  1189 00f4 00A73A5D ntdll.RtlEncodePointer(024a2090) -> ecf0f61b
  1190 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1191 00f4 00A73AD4 ntdll.RtlDecodePointer(ecf0f61b) -> 024a2090
  1192 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1193 00f4 00A73A5D ntdll.RtlEncodePointer(00c40c50) -> fa90f75c
  1194 00f4 00A73A5D ntdll.RtlEncodePointer(024a2094) -> ecf2f61b
  1195 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1196 00f4 00A73AD4 ntdll.RtlDecodePointer(ecf2f61b) -> 024a2094
  1197 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1198 00f4 00A73A5D ntdll.RtlEncodePointer(00c40d50) -> fa10f75c
  1199 00f4 00A73A5D ntdll.RtlEncodePointer(024a2098) -> ecf4f61b
  1200 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1201 00f4 00A73AD4 ntdll.RtlDecodePointer(ecf4f61b) -> 024a2098
  1202 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1203 00f4 00A73A5D ntdll.RtlEncodePointer(00c40cf0) -> fac0f75c
  1204 00f4 00A73A5D ntdll.RtlEncodePointer(024a209c) -> ecf6f61b
  1205 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1206 00f4 00A73AD4 ntdll.RtlDecodePointer(ecf6f61b) -> 024a209c
  1207 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1208 00f4 00A73A5D ntdll.RtlEncodePointer(00c40b70) -> f900f75c
  1209 00f4 00A73A5D ntdll.RtlEncodePointer(024a20a0) -> ece8f61b
  1210 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1211 00f4 00A73AD4 ntdll.RtlDecodePointer(ece8f61b) -> 024a20a0
  1212 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1213 00f4 00A73A5D ntdll.RtlEncodePointer(00c40c70) -> fa80f75c
  1214 00f4 00A73A5D ntdll.RtlEncodePointer(024a20a4) -> eceaf61b
  1215 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1216 00f4 00A73AD4 ntdll.RtlDecodePointer(eceaf61b) -> 024a20a4
  1217 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1218 00f4 00A73A5D ntdll.RtlEncodePointer(00c40b80) -> f978f75c
  1219 00f4 00A73A5D ntdll.RtlEncodePointer(024a20a8) -> ececf61b
  1220 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1221 00f4 00A73AD4 ntdll.RtlDecodePointer(ececf61b) -> 024a20a8
  1222 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1223 00f4 00A73A5D ntdll.RtlEncodePointer(00c40c90) -> faf0f75c
  1224 00f4 00A73A5D ntdll.RtlEncodePointer(024a20ac) -> eceef61b
  1225 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000030) -> 024a0eb0
  1226 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1227 00f4 00A73AD4 ntdll.RtlDecodePointer(eceef61b) -> 024a20ac
  1228 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1229 00f4 00A73A5D ntdll.RtlEncodePointer(00c40b90) -> f970f75c
  1230 00f4 00A73A5D ntdll.RtlEncodePointer(024a20b0) -> ece0f61b
  1231 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1232 00f4 00A73AD4 ntdll.RtlDecodePointer(ece0f61b) -> 024a20b0
  1233 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1234 00f4 00A73A5D ntdll.RtlEncodePointer(00c40ba0) -> f968f75c
  1235 00f4 00A73A5D ntdll.RtlEncodePointer(024a20b4) -> ece2f61b
  1236 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1237 00f4 00A73AD4 ntdll.RtlDecodePointer(ece2f61b) -> 024a20b4
  1238 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1239 00f4 00A73A5D ntdll.RtlEncodePointer(00c40bb0) -> f960f75c
  1240 00f4 00A73A5D ntdll.RtlEncodePointer(024a20b8) -> ece4f61b
  1241 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1242 00f4 00A73AD4 ntdll.RtlDecodePointer(ece4f61b) -> 024a20b8
  1243 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1244 00f4 00A73A5D ntdll.RtlEncodePointer(00c40bc0) -> f958f75c
  1245 00f4 00A73A5D ntdll.RtlEncodePointer(024a20bc) -> ece6f61b
  1246 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1247 00f4 00A73AD4 ntdll.RtlDecodePointer(ece6f61b) -> 024a20bc
  1248 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1249 00f4 00A73A5D ntdll.RtlEncodePointer(00c40e10) -> fbb0f75c
  1250 00f4 00A73A5D ntdll.RtlEncodePointer(024a20c0) -> ecd8f61b
  1251 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1252 00f4 00A73AD4 ntdll.RtlDecodePointer(ecd8f61b) -> 024a20c0
  1253 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1254 00f4 00A73A5D ntdll.RtlEncodePointer(00c40e20) -> fba8f75c
  1255 00f4 00A73A5D ntdll.RtlEncodePointer(024a20c4) -> ecdaf61b
  1256 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1257 00f4 00A73AD4 ntdll.RtlDecodePointer(ecdaf61b) -> 024a20c4
  1258 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1259 00f4 00A73A5D ntdll.RtlEncodePointer(00c40e30) -> fba0f75c
  1260 00f4 00A73A5D ntdll.RtlEncodePointer(024a20c8) -> ecdcf61b
  1261 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1262 00f4 00A73AD4 ntdll.RtlDecodePointer(ecdcf61b) -> 024a20c8
  1263 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1264 00f4 00A73A5D ntdll.RtlEncodePointer(00c40e40) -> fb98f75c
  1265 00f4 00A73A5D ntdll.RtlEncodePointer(024a20cc) -> ecdef61b
  1266 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1267 00f4 00A73AD4 ntdll.RtlDecodePointer(ecdef61b) -> 024a20cc
  1268 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1269 00f4 00A73A5D ntdll.RtlEncodePointer(00c40e60) -> fb88f75c
  1270 00f4 00A73A5D ntdll.RtlEncodePointer(024a20d0) -> ecd0f61b
  1271 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1272 00f4 00A73AD4 ntdll.RtlDecodePointer(ecd0f61b) -> 024a20d0
  1273 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1274 00f4 00A73A5D ntdll.RtlEncodePointer(00c40e80) -> fbf8f75c
  1275 00f4 00A73A5D ntdll.RtlEncodePointer(024a20d4) -> ecd2f61b
  1276 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1277 00f4 00A73AD4 ntdll.RtlDecodePointer(ecd2f61b) -> 024a20d4
  1278 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1279 00f4 00A73A5D ntdll.RtlEncodePointer(00c40e90) -> fbf0f75c
  1280 00f4 00A73A5D ntdll.RtlEncodePointer(024a20d8) -> ecd4f61b
  1281 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1282 00f4 00A73AD4 ntdll.RtlDecodePointer(ecd4f61b) -> 024a20d8
  1283 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1284 00f4 00A73A5D ntdll.RtlEncodePointer(00c40ea0) -> fbe8f75c
  1285 00f4 00A73A5D ntdll.RtlEncodePointer(024a20dc) -> ecd6f61b
  1286 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1287 00f4 00A73AD4 ntdll.RtlDecodePointer(ecd6f61b) -> 024a20dc
  1288 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1289 00f4 00A73A5D ntdll.RtlEncodePointer(00c40eb0) -> fbe0f75c
  1290 00f4 00A73A5D ntdll.RtlEncodePointer(024a20e0) -> ecc8f61b
  1291 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1292 00f4 00A73AD4 ntdll.RtlDecodePointer(ecc8f61b) -> 024a20e0
  1293 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1294 00f4 00A73A5D ntdll.RtlEncodePointer(00c40ec0) -> fbd8f75c
  1295 00f4 00A73A5D ntdll.RtlEncodePointer(024a20e4) -> eccaf61b
  1296 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1297 00f4 00A73AD4 ntdll.RtlDecodePointer(eccaf61b) -> 024a20e4
  1298 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1299 00f4 00A73A5D ntdll.RtlEncodePointer(00c40ed0) -> fbd0f75c
  1300 00f4 00A73A5D ntdll.RtlEncodePointer(024a20e8) -> ecccf61b
  1301 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1302 00f4 00A73AD4 ntdll.RtlDecodePointer(ecccf61b) -> 024a20e8
  1303 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1304 00f4 00A73A5D ntdll.RtlEncodePointer(00c40ee0) -> fbc8f75c
  1305 00f4 00A73A5D ntdll.RtlEncodePointer(024a20ec) -> eccef61b
  1306 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1307 00f4 00A73AD4 ntdll.RtlDecodePointer(eccef61b) -> 024a20ec
  1308 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1309 00f4 00A73A5D ntdll.RtlEncodePointer(00c40ef0) -> fbc0f75c
  1310 00f4 00A73A5D ntdll.RtlEncodePointer(024a20f0) -> ecc0f61b
  1311 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1312 00f4 00A73AD4 ntdll.RtlDecodePointer(ecc0f61b) -> 024a20f0
  1313 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1314 00f4 00A73A5D ntdll.RtlEncodePointer(00c40f40) -> fb18f75c
  1315 00f4 00A73A5D ntdll.RtlEncodePointer(024a20f4) -> ecc2f61b
  1316 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1317 00f4 00A73AD4 ntdll.RtlDecodePointer(ecc2f61b) -> 024a20f4
  1318 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1319 00f4 00A73A5D ntdll.RtlEncodePointer(00c40fb0) -> fb60f75c
  1320 00f4 00A73A5D ntdll.RtlEncodePointer(024a20f8) -> ecc4f61b
  1321 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1322 00f4 00A73AD4 ntdll.RtlDecodePointer(ecc4f61b) -> 024a20f8
  1323 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1324 00f4 00A73A5D ntdll.RtlEncodePointer(00c40fc0) -> fb58f75c
  1325 00f4 00A73A5D ntdll.RtlEncodePointer(024a20fc) -> ecc6f61b
  1326 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1327 00f4 00A73AD4 ntdll.RtlDecodePointer(ecc6f61b) -> 024a20fc
  1328 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1329 00f4 00A73A5D ntdll.RtlEncodePointer(00c40fe0) -> fb48f75c
  1330 00f4 00A73A5D ntdll.RtlEncodePointer(024a2100) -> ec38f61b
  1331 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1332 00f4 00A73AD4 ntdll.RtlDecodePointer(ec38f61b) -> 024a2100
  1333 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1334 00f4 00A73A5D ntdll.RtlEncodePointer(00c40fd0) -> fb50f75c
  1335 00f4 00A73A5D ntdll.RtlEncodePointer(024a2104) -> ec3af61b
  1336 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1337 00f4 00A73AD4 ntdll.RtlDecodePointer(ec3af61b) -> 024a2104
  1338 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1339 00f4 00A73A5D ntdll.RtlEncodePointer(00c41030) -> f4a0f75c
  1340 00f4 00A73A5D ntdll.RtlEncodePointer(024a2108) -> ec3cf61b
  1341 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1342 00f4 00A73AD4 ntdll.RtlDecodePointer(ec3cf61b) -> 024a2108
  1343 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1344 00f4 00A73A5D ntdll.RtlEncodePointer(00c41040) -> f498f75c
  1345 00f4 00A73A5D ntdll.RtlEncodePointer(024a210c) -> ec3ef61b
  1346 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1347 00f4 00A73AD4 ntdll.RtlDecodePointer(ec3ef61b) -> 024a210c
  1348 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1349 00f4 00A73A5D ntdll.RtlEncodePointer(00c41050) -> f490f75c
  1350 00f4 00A73A5D ntdll.RtlEncodePointer(024a2110) -> ec30f61b
  1351 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1352 00f4 00A73AD4 ntdll.RtlDecodePointer(ec30f61b) -> 024a2110
  1353 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1354 00f4 00A73A5D ntdll.RtlEncodePointer(00c41060) -> f488f75c
  1355 00f4 00A73A5D ntdll.RtlEncodePointer(024a2114) -> ec32f61b
  1356 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1357 00f4 00A73AD4 ntdll.RtlDecodePointer(ec32f61b) -> 024a2114
  1358 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1359 00f4 00A73A5D ntdll.RtlEncodePointer(00c41070) -> f480f75c
  1360 00f4 00A73A5D ntdll.RtlEncodePointer(024a2118) -> ec34f61b
  1361 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1362 00f4 00A73AD4 ntdll.RtlDecodePointer(ec34f61b) -> 024a2118
  1363 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1364 00f4 00A73A5D ntdll.RtlEncodePointer(00c41090) -> f4f0f75c
  1365 00f4 00A73A5D ntdll.RtlEncodePointer(024a211c) -> ec36f61b
  1366 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1367 00f4 00A73AD4 ntdll.RtlDecodePointer(ec36f61b) -> 024a211c
  1368 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1369 00f4 00A73A5D ntdll.RtlEncodePointer(00c41330) -> f520f75c
  1370 00f4 00A73A5D ntdll.RtlEncodePointer(024a2120) -> ec28f61b
  1371 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1372 00f4 00A73AD4 ntdll.RtlDecodePointer(ec28f61b) -> 024a2120
  1373 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1374 00f4 00A73A5D ntdll.RtlEncodePointer(00c411e0) -> f448f75c
  1375 00f4 00A73A5D ntdll.RtlEncodePointer(024a2124) -> ec2af61b
  1376 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1377 00f4 00A73AD4 ntdll.RtlDecodePointer(ec2af61b) -> 024a2124
  1378 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1379 00f4 00A73A5D ntdll.RtlEncodePointer(00c41440) -> f698f75c
  1380 00f4 00A73A5D ntdll.RtlEncodePointer(024a2128) -> ec2cf61b
  1381 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1382 00f4 00A73AD4 ntdll.RtlDecodePointer(ec2cf61b) -> 024a2128
  1383 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1384 00f4 00A73A5D ntdll.RtlEncodePointer(00c41120) -> f428f75c
  1385 00f4 00A73A5D ntdll.RtlEncodePointer(024a212c) -> ec2ef61b
  1386 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1387 00f4 00A73AD4 ntdll.RtlDecodePointer(ec2ef61b) -> 024a212c
  1388 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1389 00f4 00A73A5D ntdll.RtlEncodePointer(00c410b0) -> f4e0f75c
  1390 00f4 00A73A5D ntdll.RtlEncodePointer(024a2130) -> ec20f61b
  1391 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1392 00f4 00A73AD4 ntdll.RtlDecodePointer(ec20f61b) -> 024a2130
  1393 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1394 00f4 00A73A5D ntdll.RtlEncodePointer(00c410c0) -> f4d8f75c
  1395 00f4 00A73A5D ntdll.RtlEncodePointer(024a2134) -> ec22f61b
  1396 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1397 00f4 00A73AD4 ntdll.RtlDecodePointer(ec22f61b) -> 024a2134
  1398 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1399 00f4 00A73A5D ntdll.RtlEncodePointer(00c41200) -> f5b8f75c
  1400 00f4 00A73A5D ntdll.RtlEncodePointer(024a2138) -> ec24f61b
  1401 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1402 00f4 00A73AD4 ntdll.RtlDecodePointer(ec24f61b) -> 024a2138
  1403 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1404 00f4 00A73A5D ntdll.RtlEncodePointer(00c41230) -> f5a0f75c
  1405 00f4 00A73A5D ntdll.RtlEncodePointer(024a213c) -> ec26f61b
  1406 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1407 00f4 00A73AD4 ntdll.RtlDecodePointer(ec26f61b) -> 024a213c
  1408 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1409 00f4 00A73A5D ntdll.RtlEncodePointer(00c41250) -> f590f75c
  1410 00f4 00A73A5D ntdll.RtlEncodePointer(024a2140) -> ec18f61b
  1411 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1412 00f4 00A73AD4 ntdll.RtlDecodePointer(ec18f61b) -> 024a2140
  1413 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000200
  1414 00f4 00A6DFBF ntdll.RtlReAllocateHeap(024a0000, 00000000, 024a1f40, 00000400) -> 024a1f40
  1415 00f4 00A73A5D ntdll.RtlEncodePointer(024a1f40) -> f318f61b
  1416 00f4 00A73A5D ntdll.RtlEncodePointer(00c41270) -> f580f75c
  1417 00f4 00A73A5D ntdll.RtlEncodePointer(024a2144) -> ec1af61b
  1418 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1419 00f4 00A73AD4 ntdll.RtlDecodePointer(ec1af61b) -> 024a2144
  1420 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1421 00f4 00A73A5D ntdll.RtlEncodePointer(00c412a0) -> f5e8f75c
  1422 00f4 00A73A5D ntdll.RtlEncodePointer(024a2148) -> ec1cf61b
  1423 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1424 00f4 00A73AD4 ntdll.RtlDecodePointer(ec1cf61b) -> 024a2148
  1425 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1426 00f4 00A73A5D ntdll.RtlEncodePointer(00c412d0) -> f5d0f75c
  1427 00f4 00A73A5D ntdll.RtlEncodePointer(024a214c) -> ec1ef61b
  1428 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1429 00f4 00A73AD4 ntdll.RtlDecodePointer(ec1ef61b) -> 024a214c
  1430 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1431 00f4 00A73A5D ntdll.RtlEncodePointer(00c41300) -> f538f75c
  1432 00f4 00A73A5D ntdll.RtlEncodePointer(024a2150) -> ec10f61b
  1433 00f4 00971F08 kernel32.CreateSemaphoreA(00000000, 00000000, 7fffffff, 00000000) -> 0000008c
  1434 00f4 00971F17 kernel32.CreateSemaphoreA(00000000, 00000000, 7fffffff, 00000000) -> 00000090
  1435 00f4 00971F24 kernel32.CreateSemaphoreA(00000000, 00000000, 00000001, 00000000) -> 00000094
  1436 00f4 00971F31 kernel32.CreateSemaphoreA(00000000, 00000000, 00000001, 00000000) -> 00000098
  1437 00f4 00971F3E kernel32.CreateSemaphoreA(00000000, 00000000, 00000001, 00000000) -> 0000009c
  1438 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1439 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1440 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1441 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1442 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1443 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1444 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1445 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1446 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1447 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1448 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1449 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1450 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1451 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1452 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1453 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1454 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1455 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1456 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1457 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1458 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1459 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1460 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1461 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1462 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1463 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1464 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1465 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1466 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1467 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1468 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1469 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1470 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1471 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1472 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1473 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1474 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1475 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1476 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1477 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1478 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1479 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1480 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1481 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1482 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1483 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1484 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1485 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1486 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1487 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1488 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1489 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1490 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1491 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1492 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1493 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1494 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1495 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1496 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1497 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1498 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1499 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1500 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1501 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1502 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1503 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1504 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1505 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1506 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1507 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1508 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1509 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1510 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1511 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1512 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1513 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1514 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1515 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1516 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1517 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1518 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1519 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1520 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1521 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1522 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1523 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1524 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1525 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1526 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1527 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1528 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1529 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1530 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1531 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1532 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1533 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1534 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1535 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1536 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1537 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1538 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1539 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1540 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1541 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1542 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1543 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1544 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1545 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1546 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1547 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1548 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1549 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1550 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1551 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1552 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1553 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1554 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1555 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1556 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1557 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1558 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1559 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1560 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1561 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1562 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1563 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1564 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1565 00f4 00971F6C kernel32.ReleaseSemaphore(0000008c, 00000001, 00000000) -> 00000001
  1566 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1567 00f4 00A73AD4 ntdll.RtlDecodePointer(ec10f61b) -> 024a2150
  1568 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1569 00f4 00A73A5D ntdll.RtlEncodePointer(00c414a0) -> f6e8f75c
  1570 00f4 00A73A5D ntdll.RtlEncodePointer(024a2154) -> ec12f61b
  1571 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1572 00f4 00A73AD4 ntdll.RtlDecodePointer(ec12f61b) -> 024a2154
  1573 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1574 00f4 00A73A5D ntdll.RtlEncodePointer(00c414e0) -> f6c8f75c
  1575 00f4 00A73A5D ntdll.RtlEncodePointer(024a2158) -> ec14f61b
  1576 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1577 00f4 00A73AD4 ntdll.RtlDecodePointer(ec14f61b) -> 024a2158
  1578 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1579 00f4 00A73A5D ntdll.RtlEncodePointer(00c41460) -> f688f75c
  1580 00f4 00A73A5D ntdll.RtlEncodePointer(024a215c) -> ec16f61b
  1581 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1582 00f4 00A73AD4 ntdll.RtlDecodePointer(ec16f61b) -> 024a215c
  1583 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1584 00f4 00A73A5D ntdll.RtlEncodePointer(00c41480) -> f6f8f75c
  1585 00f4 00A73A5D ntdll.RtlEncodePointer(024a2160) -> ec08f61b
  1586 00f4 009A55C0 ntdll.RtlInitializeCriticalSection(00ed6908) -> 00000000
  1587 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1588 00f4 00A73AD4 ntdll.RtlDecodePointer(ec08f61b) -> 024a2160
  1589 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1590 00f4 00A73A5D ntdll.RtlEncodePointer(00c41430) -> f6a0f75c
  1591 00f4 00A73A5D ntdll.RtlEncodePointer(024a2164) -> ec0af61b
  1592 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1593 00f4 00A73AD4 ntdll.RtlDecodePointer(ec0af61b) -> 024a2164
  1594 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1595 00f4 00A73A5D ntdll.RtlEncodePointer(00c41350) -> f510f75c
  1596 00f4 00A73A5D ntdll.RtlEncodePointer(024a2168) -> ec0cf61b
  1597 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1598 00f4 00A73AD4 ntdll.RtlDecodePointer(ec0cf61b) -> 024a2168
  1599 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1600 00f4 00A73A5D ntdll.RtlEncodePointer(00c41130) -> f420f75c
  1601 00f4 00A73A5D ntdll.RtlEncodePointer(024a216c) -> ec0ef61b
  1602 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1603 00f4 00A73AD4 ntdll.RtlDecodePointer(ec0ef61b) -> 024a216c
  1604 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1605 00f4 00A73A5D ntdll.RtlEncodePointer(00c41150) -> f410f75c
  1606 00f4 00A73A5D ntdll.RtlEncodePointer(024a2170) -> ec00f61b
  1607 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1608 00f4 00A73AD4 ntdll.RtlDecodePointer(ec00f61b) -> 024a2170
  1609 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1610 00f4 00A73A5D ntdll.RtlEncodePointer(00c41370) -> f500f75c
  1611 00f4 00A73A5D ntdll.RtlEncodePointer(024a2174) -> ec02f61b
  1612 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1613 00f4 00A73AD4 ntdll.RtlDecodePointer(ec02f61b) -> 024a2174
  1614 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1615 00f4 00A73A5D ntdll.RtlEncodePointer(00c41410) -> f6b0f75c
  1616 00f4 00A73A5D ntdll.RtlEncodePointer(024a2178) -> ec04f61b
  1617 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1618 00f4 00A73AD4 ntdll.RtlDecodePointer(ec04f61b) -> 024a2178
  1619 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1620 00f4 00A73A5D ntdll.RtlEncodePointer(00c410d0) -> f4d0f75c
  1621 00f4 00A73A5D ntdll.RtlEncodePointer(024a217c) -> ec06f61b
  1622 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1623 00f4 00A73AD4 ntdll.RtlDecodePointer(ec06f61b) -> 024a217c
  1624 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1625 00f4 00A73A5D ntdll.RtlEncodePointer(00c41170) -> f400f75c
  1626 00f4 00A73A5D ntdll.RtlEncodePointer(024a2180) -> ec78f61b
  1627 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1628 00f4 00A73AD4 ntdll.RtlDecodePointer(ec78f61b) -> 024a2180
  1629 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1630 00f4 00A73A5D ntdll.RtlEncodePointer(00c41180) -> f478f75c
  1631 00f4 00A73A5D ntdll.RtlEncodePointer(024a2184) -> ec7af61b
  1632 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1633 00f4 00A73AD4 ntdll.RtlDecodePointer(ec7af61b) -> 024a2184
  1634 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1635 00f4 00A73A5D ntdll.RtlEncodePointer(00c41190) -> f470f75c
  1636 00f4 00A73A5D ntdll.RtlEncodePointer(024a2188) -> ec7cf61b
  1637 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1638 00f4 00A73AD4 ntdll.RtlDecodePointer(ec7cf61b) -> 024a2188
  1639 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1640 00f4 00A73A5D ntdll.RtlEncodePointer(00c413a0) -> f568f75c
  1641 00f4 00A73A5D ntdll.RtlEncodePointer(024a218c) -> ec7ef61b
  1642 00f4 009A55C0 ntdll.RtlInitializeCriticalSection(00ed6920) -> 00000000
  1643 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1644 00f4 00A73AD4 ntdll.RtlDecodePointer(ec7ef61b) -> 024a218c
  1645 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1646 00f4 00A73A5D ntdll.RtlEncodePointer(00c41420) -> f6a8f75c
  1647 00f4 00A73A5D ntdll.RtlEncodePointer(024a2190) -> ec70f61b
  1648 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1649 00f4 00A73AD4 ntdll.RtlDecodePointer(ec70f61b) -> 024a2190
  1650 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1651 00f4 00A73A5D ntdll.RtlEncodePointer(00c410e0) -> f4c8f75c
  1652 00f4 00A73A5D ntdll.RtlEncodePointer(024a2194) -> ec72f61b
  1653 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1654 00f4 00A73AD4 ntdll.RtlDecodePointer(ec72f61b) -> 024a2194
  1655 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1656 00f4 00A73A5D ntdll.RtlEncodePointer(00c411a0) -> f468f75c
  1657 00f4 00A73A5D ntdll.RtlEncodePointer(024a2198) -> ec74f61b
  1658 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1659 00f4 00A73AD4 ntdll.RtlDecodePointer(ec74f61b) -> 024a2198
  1660 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1661 00f4 00A73A5D ntdll.RtlEncodePointer(00c411b0) -> f460f75c
  1662 00f4 00A73A5D ntdll.RtlEncodePointer(024a219c) -> ec76f61b
  1663 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1664 00f4 00A73AD4 ntdll.RtlDecodePointer(ec76f61b) -> 024a219c
  1665 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1666 00f4 00A73A5D ntdll.RtlEncodePointer(00c41100) -> f438f75c
  1667 00f4 00A73A5D ntdll.RtlEncodePointer(024a21a0) -> ec68f61b
  1668 00f4 009A5173 ntdll.RtlInitializeCriticalSection(00ed6938) -> 00000000
  1669 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 01000400) -> 024c0020
  1670 00f4 0099207E ntdll.RtlInitializeCriticalSection(00ed6950) -> 00000000
  1671 00f4 0099207E ntdll.RtlInitializeCriticalSection(00ed6968) -> 00000000
  1672 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1673 00f4 00A73AD4 ntdll.RtlDecodePointer(ec68f61b) -> 024a21a0
  1674 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1675 00f4 00A73A5D ntdll.RtlEncodePointer(00c41560) -> f608f75c
  1676 00f4 00A73A5D ntdll.RtlEncodePointer(024a21a4) -> ec6af61b
  1677 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1678 00f4 00A73AD4 ntdll.RtlDecodePointer(ec6af61b) -> 024a21a4
  1679 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1680 00f4 00A73A5D ntdll.RtlEncodePointer(00c411c0) -> f458f75c
  1681 00f4 00A73A5D ntdll.RtlEncodePointer(024a21a8) -> ec6cf61b
  1682 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1683 00f4 00A73AD4 ntdll.RtlDecodePointer(ec6cf61b) -> 024a21a8
  1684 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1685 00f4 00A73A5D ntdll.RtlEncodePointer(00c413d0) -> f550f75c
  1686 00f4 00A73A5D ntdll.RtlEncodePointer(024a21ac) -> ec6ef61b
  1687 00f4 00A73AD4 ntdll.RtlDecodePointer(f318f61b) -> 024a1f40
  1688 00f4 00A73AD4 ntdll.RtlDecodePointer(ec6ef61b) -> 024a21ac
  1689 00f4 00A74272 ntdll.RtlSizeHeap(024a0000, 00000000, 024a1f40) -> 00000400
  1690 00f4 00A73A5D ntdll.RtlEncodePointer(00c415b0) -> f660f75c
  1691 00f4 00A73A5D ntdll.RtlEncodePointer(024a21b0) -> ec60f61b
  1692 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1693 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1694 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1695 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1696 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1697 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1698 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1699 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1700 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1701 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1702 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1703 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1704 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1705 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1706 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1707 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1708 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1709 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1710 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1711 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1712 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1713 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1714 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1715 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1716 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1717 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1718 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1719 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1720 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1721 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1722 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1723 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1724 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1725 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1726 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1727 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1728 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1729 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1730 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1731 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1732 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1733 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1734 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1735 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1736 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1737 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1738 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1739 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1740 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1741 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1742 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1743 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1744 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1745 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1746 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1747 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1748 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1749 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1750 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1751 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1752 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1753 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1754 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1755 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1756 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1757 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1758 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1759 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1760 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1761 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1762 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1763 00f4 00A217D2 user32.SystemParametersInfoA(0000003a, 00000008, 00ed4088, 00000000) -> 00000001
  1764 00f4 00A217DF user32.SystemParametersInfoA(00000034, 00000008, 00ed4090, 00000000) -> 00000001
  1765 00f4 00A217EC user32.SystemParametersInfoA(00000032, 00000018, 00ed4098, 00000000) -> 00000001
  1766 00f4 00944EF7 user32.SystemParametersInfoA(0000003b, 00000008, 0026fe4c, 00000000) -> 00000000
  1767 00f4 00944F22 user32.SystemParametersInfoA(00000035, 00000008, 0026fe54, 00000000) -> 00000000
  1768 00f4 00944F71 user32.SystemParametersInfoA(00000033, 00000018, 0026fe5c, 00000000) -> 00000000
  1769 00f4 00A21803 user32.SystemParametersInfoA(00000010, 00000000, 01043354, 00000002) -> 00000001
  1770 00f4 00A2180D user32.SystemParametersInfoA(00000011, 00000000, 00000000, 00000002) -> 00000001
  1771 00f4 00A21818 kernel32.SetThreadExecutionState(80000003) -> 00000007
  1772 00f4 00A21827 combase.CoInitializeEx(00000000, 00000002) -> 00000000
  1773 00f4 009703EF kernel32.GetModuleHandleA(00000000) -> 00400000
  1774 00f4 009703F6 kernel32.GetModuleFileNameA(00400000, 010d6a28, 00000104) -> 00000045
  1775 00f4 00A2166B ole32.CoInitialize(00000000) -> 00000001
  1776 00f4 00A2168B combase.CoCreateInstance(00d1c528, 00000000, 00000001, 00d1c538, 0026fc60) -> 00000000
  1778 00f4 00A216D2 kernel32.MultiByteToWideChar(00000000, 00000000, 00feea10 "Z:\\Users\\<user>\\Developer\\hp-wine-oracle\\, 00000104, 0026fc68, 00000104) -> 00000104
  1779 00f4 00A216DD oleaut32.SysAllocString(0026fc68 L"Z:\\Users\\<user>\\Developer\\hp-wine-oracle\) -> 013fea0c
  1780 00f4 00A216FE oleaut32.SysFreeString(013fea0c L"Z:\\Users\\<user>\\Developer\\hp-wine-oracle\) -> 00000000
  1781 00f4 00A21733 combase.CoUninitialize() -> 013f5218
  1782 00f4 00A21856 kernel32.SetThreadAffinityMask(fffffffe, 00000001) -> 000003ff
  1783 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 02800408) -> 034d0020
  1784 00f4 009A5173 ntdll.RtlInitializeCriticalSection(00ed6980) -> 00000000
  1785 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000004) -> 024a1be0
  1786 00f4 00A3CB50 ntdll.RtlInitializeCriticalSection(00ff2afc) -> 00000000
  1787 00f4 00A3D1FE ntdll.RtlInitializeCriticalSection(00ed6998) -> 00000000
  1788 00f4 00A3D224 kernel32.TlsAlloc() -> 00000018
  1789 00f4 009A55C0 ntdll.RtlInitializeCriticalSection(00ed69b0) -> 00000000
  1790 00f4 0098FDA4 kernel32.CreateSemaphoreA(00000000, 00000000, 00000008, 00000000) -> 000000a4
  1791 00f4 0098FDD3 kernel32.CreateThread(00000000, 00010000, 00967410, 00ed6e48, 00000004, 00ed6ef8) -> 000000a8
  1792 00f4 0098FDE4 kernel32.SetThreadPriority(000000a8, 00000000) -> 00000001
  1793 00f4 00929E83 kernel32.RaiseException(406d1388, 00000000, 00000004, 0026fbbc) -> 0026fb04
  1794 00f4 -------- thread.name(0144, Render) -> None
  1795 00f4 -------- thread.name(0144, Render) -> None
  1796 00f4 7BC12EA9 seh.406d1388(EXCEPTION_WINE_NAME_THREAD, 7BC12EA9) -> None
  1797 00f4 00A7CC5A kernel32.VirtualQuery(00dfbcf8, 0026f584, 0000001c) -> 0000001c
  1798 00f4 00A3B4E8 kernel32.CreateEventA(00000000, 00000000, 00000000, 00d1cf94 "renderThread_wakeup") -> 000000b0
  1799 00f4 00A3B503 kernel32.ResumeThread(000000a8) -> 00000001
  1800 00f4 0093C3FE ws2_32.WSAStartup(00000202, 0026faac) -> 00000000
  1801 00f4 0093C415 kernel32.InterlockedIncrement(01042e6c) -> 00000001
  1802 00f4 0093C450 ws2_32.gethostname(0026faac, 000000ff) -> 00000000
  1803 00f4 0093C45B ws2_32.gethostbyname(0026faac "Macintosh.local") -> 0015a530
  1804 0144 00A3B42B kernel32.WaitForSingleObject(000000b0, ffffffff) -> None
  1808 00f4 0093C46D ws2_32.inet_ntoa(020a14ac) -> 0015ac60
  1809 00f4 0093C489 kernel32.InterlockedDecrement(01042e6c) -> 00000000
  1810 00f4 0093C4A1 ws2_32.WSACleanup() -> 00000000
  1811 00f4 00A2C7EB d3d9.Direct3DCreate9(00000020) -> 01400828
  1812 00f4 009F40A2 ntdll.RtlInitializeCriticalSection(00ed69c8) -> 00000000
  1813 00f4 009B2625 kernel32.CreateEventA(00000000, 00000000, 00000000, 00000000) -> 000000b8
  1814 00f4 00999062 kernel32.CreateSemaphoreA(00000000, 00000000, 00000010, 00000000) -> 000000bc
  1815 00f4 00999085 ntdll.RtlInitializeCriticalSection(00ed69e0) -> 00000000
  1816 00f4 00999113 kernel32.CreateSemaphoreA(00000000, 00000000, 00000020, 00000000) -> 000000c0
  1817 00f4 0099913C ntdll.RtlInitializeCriticalSection(00ed69f8) -> 00000000
  1818 00f4 00999192 kernel32.CreateSemaphoreA(00000000, 00000000, 00000011, 00000000) -> 000000c4
  1819 00f4 009991B5 ntdll.RtlInitializeCriticalSection(00ed6a10) -> 00000000
  1820 00f4 00999243 kernel32.CreateSemaphoreA(00000000, 00000000, 00000020, 00000000) -> 000000c8
  1821 00f4 0099926C ntdll.RtlInitializeCriticalSection(00ed6a28) -> 00000000
  1822 00f4 00970E1C kernel32.ReleaseSemaphore(000000bc, 00000001, 00000000) -> 00000001
  1823 00f4 00970E1C kernel32.ReleaseSemaphore(000000bc, 00000001, 00000000) -> 00000001
  1824 00f4 00970E1C kernel32.ReleaseSemaphore(000000bc, 00000001, 00000000) -> 00000001
  1825 00f4 00970E1C kernel32.ReleaseSemaphore(000000bc, 00000001, 00000000) -> 00000001
  1826 00f4 00970E1C kernel32.ReleaseSemaphore(000000bc, 00000001, 00000000) -> 00000001
  1827 00f4 00970E1C kernel32.ReleaseSemaphore(000000bc, 00000001, 00000000) -> 00000001
  1828 00f4 00970E1C kernel32.ReleaseSemaphore(000000bc, 00000001, 00000000) -> 00000001
  1829 00f4 00970E1C kernel32.ReleaseSemaphore(000000bc, 00000001, 00000000) -> 00000001
  1830 00f4 00970E1C kernel32.ReleaseSemaphore(000000bc, 00000001, 00000000) -> 00000001
  1831 00f4 00970E1C kernel32.ReleaseSemaphore(000000bc, 00000001, 00000000) -> 00000001
  1832 00f4 00970E1C kernel32.ReleaseSemaphore(000000bc, 00000001, 00000000) -> 00000001
  1833 00f4 00970E1C kernel32.ReleaseSemaphore(000000bc, 00000001, 00000000) -> 00000001
  1834 00f4 00970E1C kernel32.ReleaseSemaphore(000000bc, 00000001, 00000000) -> 00000001
  1835 00f4 00970E1C kernel32.ReleaseSemaphore(000000bc, 00000001, 00000000) -> 00000001
  1836 00f4 00970E1C kernel32.ReleaseSemaphore(000000bc, 00000001, 00000000) -> 00000001
  1837 00f4 00970E1C kernel32.ReleaseSemaphore(000000bc, 00000001, 00000000) -> 00000001
  1838 00f4 009710EC kernel32.ReleaseSemaphore(000000c4, 00000001, 00000000) -> 00000001
  1839 00f4 009710EC kernel32.ReleaseSemaphore(000000c4, 00000001, 00000000) -> 00000001
  1840 00f4 009710EC kernel32.ReleaseSemaphore(000000c4, 00000001, 00000000) -> 00000001
  1841 00f4 009710EC kernel32.ReleaseSemaphore(000000c4, 00000001, 00000000) -> 00000001
  1842 00f4 009710EC kernel32.ReleaseSemaphore(000000c4, 00000001, 00000000) -> 00000001
  1843 00f4 009710EC kernel32.ReleaseSemaphore(000000c4, 00000001, 00000000) -> 00000001
  1844 00f4 009710EC kernel32.ReleaseSemaphore(000000c4, 00000001, 00000000) -> 00000001
  1845 00f4 009710EC kernel32.ReleaseSemaphore(000000c4, 00000001, 00000000) -> 00000001
  1846 00f4 009710EC kernel32.ReleaseSemaphore(000000c4, 00000001, 00000000) -> 00000001
  1847 00f4 009710EC kernel32.ReleaseSemaphore(000000c4, 00000001, 00000000) -> 00000001
  1848 00f4 009710EC kernel32.ReleaseSemaphore(000000c4, 00000001, 00000000) -> 00000001
  1849 00f4 009710EC kernel32.ReleaseSemaphore(000000c4, 00000001, 00000000) -> 00000001
  1850 00f4 009710EC kernel32.ReleaseSemaphore(000000c4, 00000001, 00000000) -> 00000001
  1851 00f4 009710EC kernel32.ReleaseSemaphore(000000c4, 00000001, 00000000) -> 00000001
  1852 00f4 009710EC kernel32.ReleaseSemaphore(000000c4, 00000001, 00000000) -> 00000001
  1853 00f4 009710EC kernel32.ReleaseSemaphore(000000c4, 00000001, 00000000) -> 00000001
  1854 00f4 009710EC kernel32.ReleaseSemaphore(000000c4, 00000001, 00000000) -> 00000001
  1855 00f4 0098FDA4 kernel32.CreateSemaphoreA(00000000, 00000000, 00000008, 00000000) -> 000000cc
  1856 00f4 0098FDD3 kernel32.CreateThread(00000000, 00000000, 00967410, 00ed6efc, 00000004, 00ed6fac) -> 000000d0
  1857 00f4 0098FDE4 kernel32.SetThreadPriority(000000d0, 00000001) -> 00000001
  1858 00f4 00929E83 kernel32.RaiseException(406d1388, 00000000, 00000004, 0026fb7c) -> 0026fac4
  1859 00f4 -------- thread.name(0148, m_readThread) -> None
  1860 00f4 -------- thread.name(0148, m_readThread) -> None
  1861 00f4 7BC12EA9 seh.406d1388(EXCEPTION_WINE_NAME_THREAD, 7BC12EA9) -> None
  1862 00f4 0098FE14 kernel32.ResumeThread(000000d0) -> 00000001
  1863 00f4 0098FDA4 kernel32.CreateSemaphoreA(00000000, 00000000, 00000008, 00000000) -> 000000d4
  1864 00f4 0098FDD3 kernel32.CreateThread(00000000, 00000000, 00967410, 00ed6fb0, 00000004, 00ed7060) -> 000000dc
  1865 00f4 0098FDE4 kernel32.SetThreadPriority(000000dc, ffffffff) -> 00000001
  1866 00f4 00929E83 kernel32.RaiseException(406d1388, 00000000, 00000004, 0026fb78) -> 0026fac4
  1867 00f4 -------- thread.name(014c, m_decodeThread) -> None
  1868 0148 0098E859 kernel32.InterlockedIncrement(034bb9bc) -> 00000001
  1869 0148 0098E865 kernel32.WaitForSingleObject(000000b8, ffffffff) -> 00000000
  1870 00f4 -------- thread.name(014c, m_decodeThread) -> None
  1871 00f4 7BC12EA9 seh.406d1388(EXCEPTION_WINE_NAME_THREAD, 7BC12EA9) -> None
  1872 00f4 0098FE14 kernel32.ResumeThread(000000dc) -> 00000001
  1873 00f4 009A7348 ntdll.RtlInitializeCriticalSection(00ed6a40) -> 00000000
  1874 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000008) -> 024a1c00
  1875 00f4 009A7387 kernel32.CreateSemaphoreA(00000000, 00000000, 00000001, 00000000) -> 000000d8
  1876 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000008) -> 024a1c20
  1877 00f4 009A73AD kernel32.CreateSemaphoreA(00000000, 00000000, 00000001, 00000000) -> 000000e0
  1878 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000008) -> 024a1c40
  1879 00f4 009A7387 kernel32.CreateSemaphoreA(00000000, 00000000, 00000001, 00000000) -> 000000e4
  1880 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000008) -> 024a1c60
  1881 00f4 009A73AD kernel32.CreateSemaphoreA(00000000, 00000000, 00000001, 00000000) -> 000000e8
  1882 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000008) -> 024a1c80
  1883 00f4 009A7387 kernel32.CreateSemaphoreA(00000000, 00000000, 00000001, 00000000) -> 000000ec
  1884 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000008) -> 024a1ca0
  1885 00f4 009A73AD kernel32.CreateSemaphoreA(00000000, 00000000, 00000001, 00000000) -> 000000f0
  1886 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000008) -> 024a1cc0
  1887 00f4 009A7387 kernel32.CreateSemaphoreA(00000000, 00000000, 00000001, 00000000) -> 000000f4
  1888 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000008) -> 024a1ce0
  1889 014c 00970EFD kernel32.WaitForSingleObject(000000c0, ffffffff) -> 00000000
  1890 00f4 009A73AD kernel32.CreateSemaphoreA(00000000, 00000000, 00000001, 00000000) -> 000000f8
  1891 00f4 009A7887 ntdll.RtlInitializeCriticalSection(00ed6a58) -> 00000000
  1892 00f4 009A78B9 ntdll.RtlInitializeCriticalSection(00ed6a70) -> 00000000
  1893 00f4 0092A04B kernel32.GetProcessAffinityMask(ffffffff, 0026fc38, 0026fc3c) -> 00000001
  1894 00f4 0092A05F kernel32.SetProcessAffinityMask(ffffffff, 00000001) -> 00000001
  1895 00f4 0092A06A kernel32.QueryPerformanceCounter(0101b418) -> 00000001
  1896 00f4 0092A075 kernel32.QueryPerformanceFrequency(00f962e0) -> 00000001
  1897 00f4 0092A07F kernel32.SetProcessAffinityMask(ffffffff, 000003ff) -> 00000001
  1898 00f4 009452A7 shell32.SHGetFolderPathA(00000000, 0000001a, 00000000, 00000000, 010d5620) -> 00000000
  1899 00f4 0094538C kernel32.GetFileAttributesA(00ed8494 "C:\\users\\<user>\\AppData\\Roaming\\WB Games\) -> 00000010
  1900 00f4 009A7ECD ntdll.RtlInitializeCriticalSection(00ed6a88) -> 00000000
  1901 00f4 009A7F1D kernel32.CreateFileA(0026f6f4 "C:\\users\\<user>\\AppData\\Roaming\\WB Games\, 80000000, 00000001, 00000000, 00000003, 00000000, 00000000) -> 000000fc
  1902 00f4 009A692D ntdll.RtlInitializeCriticalSection(00ed6aa0) -> 00000000
  1903 00f4 00967055 kernel32.SetFilePointerEx(000000fc, 000000000, 0026fae8, 00000002) -> 00000001
  1904 00f4 00929AE6 kernel32.SetFilePointerEx(000000fc, 000000000, 0026faf4, 00000001) -> 00000001
  1905 00f4 009A7E36 kernel32.CloseHandle(000000fc) -> 00000001
  1906 00f4 00960A0B kernel32.InterlockedCompareExchange(0104342c, 010437a0, 00000000) -> 00000000
  1907 00f4 009A7F1D kernel32.CreateFileA(0026f708 "C:\\users\\<user>\\AppData\\Roaming\\WB Games\, 80000000, 00000001, 00000000, 00000003, 00000000, 00000000) -> 000000fc
  1908 00f4 00960A3A kernel32.InterlockedCompareExchange(0104342c, 00000000, 010437a0) -> 010437a0
  1909 00f4 00966FC7 kernel32.ReadFile(000000fc, 0026fb04, 00000002, 0026fae4, 00000000) -> 00000001
  1910 00f4 00967055 kernel32.SetFilePointerEx(000000fc, 000000000, 0026fadc, 00000000) -> 00000001
  1911 00f4 00929B1D kernel32.GetFileSizeEx(000000fc, 0026faf0) -> 00000001
  1912 00f4 00966FC7 kernel32.ReadFile(000000fc, 00f9db30, 0000052c, 0026fae8, 00000000) -> 00000001
  1913 00f4 009A7E36 kernel32.CloseHandle(000000fc) -> 00000001
  1914 00f4 00960A0B kernel32.InterlockedCompareExchange(0104342c, 010437a0, 00000000) -> 00000000
  1915 00f4 009A7F1D kernel32.CreateFileA(0026f814 "__DISC__", 80000000, 00000001, 00000000, 00000003, 00000000, 00000000) -> 000000fc
  1916 00f4 00960A3A kernel32.InterlockedCompareExchange(0104342c, 00000000, 010437a0) -> 010437a0
  1917 00f4 00967055 kernel32.SetFilePointerEx(000000fc, 000000000, 0026fc08, 00000002) -> 00000001
  1918 00f4 00929AE6 kernel32.SetFilePointerEx(000000fc, 000000000, 0026fc14, 00000001) -> 00000001
  1919 00f4 009A7E36 kernel32.CloseHandle(000000fc) -> 00000001
  1920 00f4 00960A0B kernel32.InterlockedCompareExchange(0104342c, 010437a0, 00000000) -> 00000000
  1921 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1922 00f4 009A7F1D kernel32.CreateFileA(0026f7ac "upd:\\PATCH.DAT", 80000000, 00000001, 00000000, 00000003, 00000000, 00000000) -> ffffffff
  1923 00f4 00A73C51 kernel32.GetLastError() -> 00000003
  1924 00f4 009A7F1D kernel32.CreateFileA(0026f7ac "GAME.DAT", 80000000, 00000001, 00000000, 00000003, 00000000, 00000000) -> 000000fc
  1925 00f4 00960A3A kernel32.InterlockedCompareExchange(0104342c, 00000000, 010437a0) -> 010437a0
  1926 00f4 00966FC7 kernel32.ReadFile(000000fc, 0026fbe4, 00000008, 0026fbbc, 00000000) -> 00000001
  1927 00f4 00967055 kernel32.SetFilePointerEx(000000fc, 062cf1689, 0026fbb4, 00000000) -> 00000001
  1928 00f4 00966FC7 kernel32.ReadFile(000000fc, 03355ec0, 00000004, 0026fbbc, 00000000) -> 00000001
  1929 00f4 00966FC7 kernel32.ReadFile(000000fc, 03355ec4, 00000004, 0026fbbc, 00000000) -> 00000001
  1930 00f4 00966FC7 kernel32.ReadFile(000000fc, 0333ffa0, 00015f00, 0026fbbc, 00000000) -> 00000001
  1931 00f4 00966FC7 kernel32.ReadFile(000000fc, 03355ecc, 00000004, 0026fbbc, 00000000) -> 00000001
  1932 00f4 00966FC7 kernel32.ReadFile(000000fc, 024c0028, 0000c1e8, 0026fbbc, 00000000) -> 00000001
  1933 00f4 00966FC7 kernel32.ReadFile(000000fc, 03355ed4, 00000004, 0026fbbc, 00000000) -> 00000001
  1934 00f4 00966FC7 kernel32.ReadFile(000000fc, 024cc218, 0001de30, 0026fbbc, 00000000) -> 00000001
  1935 00f4 00966FC7 kernel32.ReadFile(000000fc, 0333a7c0, 000057c0, 0026fbbc, 00000000) -> 00000001
  1936 00f4 00966FC7 kernel32.ReadFile(000000fc, 03355ee0, 00000004, 0026fbbc, 00000000) -> 00000001
  1937 00f4 00966FC7 kernel32.ReadFile(000000fc, 03355ee4, 00000004, 0026fbbc, 00000000) -> 00000001
  1938 00f4 00966FC7 kernel32.ReadFile(000000fc, 034d0060, 00000000, 0026fbbc, 00000000) -> 00000001
  1939 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1940 00f4 009A7F1D kernel32.CreateFileA(0026f7ac "GAME0.DAT", 80000000, 00000001, 00000000, 00000003, 00000000, 00000000) -> ffffffff
  1941 00f4 00A73C51 kernel32.GetLastError() -> 00000002
  1942 00f4 009A7F1D kernel32.CreateFileA(0026f7ac "GAME1.DAT", 80000000, 00000001, 00000000, 00000003, 00000000, 00000000) -> 00000100
  1943 00f4 00966FC7 kernel32.ReadFile(00000100, 0026fbe4, 00000008, 0026fbbc, 00000000) -> 00000001
  1944 00f4 00967055 kernel32.SetFilePointerEx(00000100, 0330acc4f, 0026fbb4, 00000000) -> 00000001
  1945 00f4 00966FC7 kernel32.ReadFile(00000100, 0333a6f8, 00000004, 0026fbbc, 00000000) -> 00000001
  1946 00f4 00966FC7 kernel32.ReadFile(00000100, 0333a6fc, 00000004, 0026fbbc, 00000000) -> 00000001
  1947 00f4 00966FC7 kernel32.ReadFile(00000100, 03335dc0, 00004930, 0026fbbc, 00000000) -> 00000001
  1948 00f4 00966FC7 kernel32.ReadFile(00000100, 0333a704, 00000004, 0026fbbc, 00000000) -> 00000001
  1949 00f4 00966FC7 kernel32.ReadFile(00000100, 024c0028, 00002828, 0026fbbc, 00000000) -> 00000001
  1950 00f4 00966FC7 kernel32.ReadFile(00000100, 0333a70c, 00000004, 0026fbbc, 00000000) -> 00000001
  1951 00f4 00966FC7 kernel32.ReadFile(00000100, 024c2858, 00005008, 0026fbbc, 00000000) -> 00000001
  1952 00f4 00966FC7 kernel32.ReadFile(00000100, 03334b60, 0000124c, 0026fbbc, 00000000) -> 00000001
  1953 00f4 00966FC7 kernel32.ReadFile(00000100, 0333a718, 00000004, 0026fbbc, 00000000) -> 00000001
  1954 00f4 00966FC7 kernel32.ReadFile(00000100, 0333a71c, 00000004, 0026fbbc, 00000000) -> 00000001
  1955 00f4 00966FC7 kernel32.ReadFile(00000100, 034d0040, 00000000, 0026fbbc, 00000000) -> 00000001
  1956 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1957 00f4 009A7F1D kernel32.CreateFileA(0026f7ac "GAME2.DAT", 80000000, 00000001, 00000000, 00000003, 00000000, 00000000) -> 00000104
  1958 00f4 00966FC7 kernel32.ReadFile(00000104, 0026fbe4, 00000008, 0026fbbc, 00000000) -> 00000001
  1959 00f4 00967055 kernel32.SetFilePointerEx(00000104, 03ebd1e34, 0026fbb4, 00000000) -> 00000001
  1960 00f4 00966FC7 kernel32.ReadFile(00000104, 03334a98, 00000004, 0026fbbc, 00000000) -> 00000001
  1961 00f4 00966FC7 kernel32.ReadFile(00000104, 03334a9c, 00000004, 0026fbbc, 00000000) -> 00000001
  1962 00f4 00966FC7 kernel32.ReadFile(00000104, 033304a0, 000045f0, 0026fbbc, 00000000) -> 00000001
  1963 00f4 00966FC7 kernel32.ReadFile(00000104, 03334aa4, 00000004, 0026fbbc, 00000000) -> 00000001
  1964 00f4 00966FC7 kernel32.ReadFile(00000104, 024c0028, 000026e8, 0026fbbc, 00000000) -> 00000001
  1965 00f4 00966FC7 kernel32.ReadFile(00000104, 03334aac, 00000004, 0026fbbc, 00000000) -> 00000001
  1966 00f4 00966FC7 kernel32.ReadFile(00000104, 024c2718, 00004cac, 0026fbbc, 00000000) -> 00000001
  1967 00f4 00966FC7 kernel32.ReadFile(00000104, 0332f300, 0000117c, 0026fbbc, 00000000) -> 00000001
  1968 00f4 00966FC7 kernel32.ReadFile(00000104, 03334ab8, 00000004, 0026fbbc, 00000000) -> 00000001
  1969 00f4 00966FC7 kernel32.ReadFile(00000104, 03334abc, 00000004, 0026fbbc, 00000000) -> 00000001
  1970 00f4 00966FC7 kernel32.ReadFile(00000104, 0332f2e0, 00000000, 0026fbbc, 00000000) -> 00000001
  1971 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1972 00f4 009A7F1D kernel32.CreateFileA(0026f7ac "GAME3.DAT", 80000000, 00000001, 00000000, 00000003, 00000000, 00000000) -> 00000108
  1973 00f4 00966FC7 kernel32.ReadFile(00000108, 0026fbe4, 00000008, 0026fbbc, 00000000) -> 00000001
  1974 00f4 00967055 kernel32.SetFilePointerEx(00000108, 049688a4f, 0026fbb4, 00000000) -> 00000001
  1975 00f4 00966FC7 kernel32.ReadFile(00000108, 0332f218, 00000004, 0026fbbc, 00000000) -> 00000001
  1976 00f4 00966FC7 kernel32.ReadFile(00000108, 0332f21c, 00000004, 0026fbbc, 00000000) -> 00000001
  1977 00f4 00966FC7 kernel32.ReadFile(00000108, 03329da0, 00005470, 0026fbbc, 00000000) -> 00000001
  1978 00f4 00966FC7 kernel32.ReadFile(00000108, 0332f224, 00000004, 0026fbbc, 00000000) -> 00000001
  1979 00f4 00966FC7 kernel32.ReadFile(00000108, 024c0028, 00002ea8, 0026fbbc, 00000000) -> 00000001
  1980 00f4 00966FC7 kernel32.ReadFile(00000108, 0332f22c, 00000004, 0026fbbc, 00000000) -> 00000001
  1981 00f4 00966FC7 kernel32.ReadFile(00000108, 024c2ed8, 00005a78, 0026fbbc, 00000000) -> 00000001
  1982 00f4 00966FC7 kernel32.ReadFile(00000108, 03328860, 0000151c, 0026fbbc, 00000000) -> 00000001
  1983 00f4 00966FC7 kernel32.ReadFile(00000108, 0332f238, 00000004, 0026fbbc, 00000000) -> 00000001
  1984 00f4 00966FC7 kernel32.ReadFile(00000108, 0332f23c, 00000004, 0026fbbc, 00000000) -> 00000001
  1985 00f4 00966FC7 kernel32.ReadFile(00000108, 03328840, 00000000, 0026fbbc, 00000000) -> 00000001
  1986 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  1987 00f4 009A7F1D kernel32.CreateFileA(0026f7ac "GAME4.DAT", 80000000, 00000001, 00000000, 00000003, 00000000, 00000000) -> 0000010c
  1988 00f4 00966FC7 kernel32.ReadFile(0000010c, 0026fbe4, 00000008, 0026fbbc, 00000000) -> 00000001
  1989 00f4 00967055 kernel32.SetFilePointerEx(0000010c, 037245ccb, 0026fbb4, 00000000) -> 00000001
  1990 00f4 00966FC7 kernel32.ReadFile(0000010c, 03328778, 00000004, 0026fbbc, 00000000) -> 00000001
  1991 00f4 00966FC7 kernel32.ReadFile(0000010c, 0332877c, 00000004, 0026fbbc, 00000000) -> 00000001
  1992 00f4 00966FC7 kernel32.ReadFile(0000010c, 03324360, 000043f0, 0026fbbc, 00000000) -> 00000001
  1993 00f4 00966FC7 kernel32.ReadFile(0000010c, 03328784, 00000004, 0026fbbc, 00000000) -> 00000001
  1994 00f4 00966FC7 kernel32.ReadFile(0000010c, 024c0028, 000025d8, 0026fbbc, 00000000) -> 00000001
  1995 00f4 00966FC7 kernel32.ReadFile(0000010c, 0332878c, 00000004, 0026fbbc, 00000000) -> 00000001
  1996 00f4 00966FC7 kernel32.ReadFile(0000010c, 024c2608, 0000458c, 0026fbbc, 00000000) -> 00000001
  1997 00f4 00966FC7 kernel32.ReadFile(0000010c, 03323240, 000010fc, 0026fbbc, 00000000) -> 00000001
  1998 00f4 00966FC7 kernel32.ReadFile(0000010c, 03328798, 00000004, 0026fbbc, 00000000) -> 00000001
  1999 00f4 00966FC7 kernel32.ReadFile(0000010c, 0332879c, 00000004, 0026fbbc, 00000000) -> 00000001
  2000 00f4 00966FC7 kernel32.ReadFile(0000010c, 03323220, 00000000, 0026fbbc, 00000000) -> 00000001
  2001 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  2002 00f4 009A7F1D kernel32.CreateFileA(0026f7ac "GAME5.DAT", 80000000, 00000001, 00000000, 00000003, 00000000, 00000000) -> 00000110
  2003 00f4 00966FC7 kernel32.ReadFile(00000110, 0026fbe4, 00000008, 0026fbbc, 00000000) -> 00000001
  2004 00f4 00967055 kernel32.SetFilePointerEx(00000110, 010065d5e, 0026fbb4, 00000000) -> 00000001
  2005 00f4 00966FC7 kernel32.ReadFile(00000110, 03323158, 00000004, 0026fbbc, 00000000) -> 00000001
  2006 00f4 00966FC7 kernel32.ReadFile(00000110, 0332315c, 00000004, 0026fbbc, 00000000) -> 00000001
  2007 00f4 00966FC7 kernel32.ReadFile(00000110, 03320760, 000029c0, 0026fbbc, 00000000) -> 00000001
  2008 00f4 00966FC7 kernel32.ReadFile(00000110, 03323164, 00000004, 0026fbbc, 00000000) -> 00000001
  2009 00f4 00966FC7 kernel32.ReadFile(00000110, 024c0028, 00001778, 0026fbbc, 00000000) -> 00000001
  2010 00f4 00966FC7 kernel32.ReadFile(00000110, 0332316c, 00000004, 0026fbbc, 00000000) -> 00000001
  2011 00f4 00966FC7 kernel32.ReadFile(00000110, 024c17a8, 00004924, 0026fbbc, 00000000) -> 00000001
  2012 00f4 00966FC7 kernel32.ReadFile(00000110, 0331fce0, 00000a70, 0026fbbc, 00000000) -> 00000001
  2013 00f4 00966FC7 kernel32.ReadFile(00000110, 03323178, 00000004, 0026fbbc, 00000000) -> 00000001
  2014 00f4 00966FC7 kernel32.ReadFile(00000110, 0332317c, 00000004, 0026fbbc, 00000000) -> 00000001
  2015 00f4 00966FC7 kernel32.ReadFile(00000110, 0331fcc0, 00000000, 0026fbbc, 00000000) -> 00000001
  2016 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  2017 00f4 009A7F1D kernel32.CreateFileA(0026f7ac "GAME6.DAT", 80000000, 00000001, 00000000, 00000003, 00000000, 00000000) -> 00000114
  2018 00f4 00966FC7 kernel32.ReadFile(00000114, 0026fbe4, 00000008, 0026fbbc, 00000000) -> 00000001
  2019 00f4 00967055 kernel32.SetFilePointerEx(00000114, 00c6c71b1, 0026fbb4, 00000000) -> 00000001
  2020 00f4 00966FC7 kernel32.ReadFile(00000114, 0331fbf8, 00000004, 0026fbbc, 00000000) -> 00000001
  2021 00f4 00966FC7 kernel32.ReadFile(00000114, 0331fbfc, 00000004, 0026fbbc, 00000000) -> 00000001
  2022 00f4 00966FC7 kernel32.ReadFile(00000114, 0331f2c0, 00000920, 0026fbbc, 00000000) -> 00000001
  2023 00f4 00966FC7 kernel32.ReadFile(00000114, 0331fc04, 00000004, 0026fbbc, 00000000) -> 00000001
  2024 00f4 00966FC7 kernel32.ReadFile(00000114, 024c0028, 000004a8, 0026fbbc, 00000000) -> 00000001
  2025 00f4 00966FC7 kernel32.ReadFile(00000114, 0331fc0c, 00000004, 0026fbbc, 00000000) -> 00000001
  2026 00f4 00966FC7 kernel32.ReadFile(00000114, 024c04d8, 00000c58, 0026fbbc, 00000000) -> 00000001
  2027 00f4 00966FC7 kernel32.ReadFile(00000114, 0331f060, 00000248, 0026fbbc, 00000000) -> 00000001
  2028 00f4 00966FC7 kernel32.ReadFile(00000114, 0331fc18, 00000004, 0026fbbc, 00000000) -> 00000001
  2029 00f4 00966FC7 kernel32.ReadFile(00000114, 0331fc1c, 00000004, 0026fbbc, 00000000) -> 00000001
  2030 00f4 00966FC7 kernel32.ReadFile(00000114, 0331f040, 00000000, 0026fbbc, 00000000) -> 00000001
  2031 00f4 00A73C51 kernel32.GetLastError() -> 00000000
  2032 00f4 009A7F1D kernel32.CreateFileA(0026f7ac "GAME7.DAT", 80000000, 00000001, 00000000, 00000003, 00000000, 00000000) -> ffffffff
  2033 00f4 00A73C51 kernel32.GetLastError() -> 00000002
  2034 00f4 009A7F1D kernel32.CreateFileA(0026f7ac "GAME8.DAT", 80000000, 00000001, 00000000, 00000003, 00000000, 00000000) -> ffffffff
  2035 00f4 0096FDF7 advapi32.RegOpenKeyExA(80000002, 0026fb3c "SOFTWARE\\WB Games\\LEGOHarryPotter", 00000000, 00000001, 0026fb34) -> 00000002
  2036 00f4 0096FE30 kernel32.GetUserDefaultLangID() -> 00000409
  2037 00f4 009EB307 user32.LoadIconA(00400000, 00000065) -> 00020060
  2038 00f4 009EB378 user32.LoadCursorA(00000000, 00007f00) -> 0002005c
  2039 00f4 009EB3A2 user32.RegisterClassExA(00ff1c44) -> 0000c039
  2040 00f4 009EB423 user32.AdjustWindowRect(0026fc24, 00cf0000, 00000000) -> 00000001
  2041 00f4 009EB45F user32.CreateWindowExA(00000000, 00d1bd94 "TTalesWindow", 00c8c250 "LEGO\xae Harry Potter\x99", 00cf0000, fffffffc, ffffffe2, 00000408, 00000322, 00000000, 00000000, 00400000, 00000000) -> 00020064
  2042 00f4 009EB128 ntdll.NtdllDefWindowProc_A(00020064, 00000024, 00000000, 0026f60c) -> 00000000
  2043 00f4 009EB128 ntdll.NtdllDefWindowProc_A(00020064, 00000081, 00000000, 0026f450) -> 00000001
  2044 00f4 009EB128 ntdll.NtdllDefWindowProc_A(00020064, 00000083, 00000000, 0026f61c) -> 00000000
  2045 00f4 009EB128 ntdll.NtdllDefWindowProc_A(00020064, 00000001, 00000000, 0026f450) -> 00000000
  2046 00f4 00A73C51 kernel32.GetLastError() -> 00000578
  2047 00f4 009450D6 kernel32.CreateMutexA(00000000, 00000001, 0026fa08 "TTALES-{LEGO\xae Harry Potter\x99}") -> 00000118
  2048 00f4 009450DE kernel32.GetLastError() -> 00000000
  2049 00f4 009450F1 kernel32.ReleaseMutex(00000118) -> 00000001
  2050 00f4 00945147 user32.ShowWindow(00020064, 00000001) -> 00000000
  2051 00f4 009EB128 ntdll.NtdllDefWindowProc_A(00020064, 00000018, 00000001, 00000000) -> 00000000
  2052 00f4 009EB128 ntdll.NtdllDefWindowProc_A(00020064, 00000046, 00000000, 0026f9ac) -> 00000000
  2053 00f4 009EB128 ntdll.NtdllDefWindowProc_A(00020064, 0000007f, 00000001, 00000000) -> 00000000
  2054 00f4 009EB128 ntdll.NtdllDefWindowProc_A(00020064, 0000007f, 00000000, 00000000) -> 00000000
  2055 00f4 009EB128 ntdll.NtdllDefWindowProc_A(00020064, 0000030f, 00000000, 00000000) -> 00000000
  2056 00f4 009EB128 ntdll.NtdllDefWindowProc_A(00020064, 00000086, 00000001, 00000000) -> 00000001
  2057 00f4 009EB142 ntdll.NtdllDefWindowProc_A(00020064, 00000281, 00000001, 00000000) -> 00000000
  2058 00f4 009EB128 ntdll.NtdllDefWindowProc_A(00020064, 00000007, 00000000, 00000000) -> 00000000
  2059 00f4 009EB128 ntdll.NtdllDefWindowProc_A(00020064, 00000085, 00000001, 00000000) -> 00000000
  2060 00f4 009EB128 ntdll.NtdllDefWindowProc_A(00020064, 00000014, 1601004a, 00000000) -> 00000001
  2061 00f4 009EB128 ntdll.NtdllDefWindowProc_A(00020064, 00000047, 00000000, 0026f9ac) -> 00000000
  2062 00f4 0094514E user32.UpdateWindow(00020064) -> 00000001
  2063 00f4 009EB128 ntdll.NtdllDefWindowProc_A(00020064, 0000000f, 00000000, 00000000) -> 00000000
  2064 00f4 009EB4A1 user32.ShowWindow(00020064, 00000001) -> 00000001
  2065 00f4 009EB4A8 user32.UpdateWindow(00020064) -> 00000001
  2066 00f4 009EB4B9 user32.SendMessageA(00020064, 00000080, 00000001, 00020060) -> 00000000
  2067 00f4 009EB128 ntdll.NtdllDefWindowProc_A(00020064, 00000080, 00000001, 00020060) -> 00000000
  2068 00f4 009EB4C3 user32.SendMessageA(00020064, 00000080, 00000000, 00020060) -> 00000000
  2069 00f4 009EB128 ntdll.NtdllDefWindowProc_A(00020064, 00000080, 00000000, 00020060) -> 00000000
  2070 00f4 009EB4D7 user32.EnumDisplayMonitors(00000000, 00000000, 00945080, 00000000) -> 00000001
  2071 00f4 00A6A8AE ntdll.RtlAllocateHeap(024a0000, 00000000, 00000008) -> 024a1d00
  2072 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0478, 00000000) -> 00000000
  2073 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a04c8, 032a0478) -> 032a0478
  2074 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0518, 032a04c8) -> 032a04c8
  2075 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0568, 032a0518) -> 032a0518
  2076 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a05b8, 032a0568) -> 032a0568
  2077 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0608, 032a05b8) -> 032a05b8
  2078 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0658, 032a0608) -> 032a0608
  2079 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a06a8, 032a0658) -> 032a0658
  2080 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a06f8, 032a06a8) -> 032a06a8
  2081 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0748, 032a06f8) -> 032a06f8
  2082 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0798, 032a0748) -> 032a0748
  2083 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a07e8, 032a0798) -> 032a0798
  2084 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0838, 032a07e8) -> 032a07e8
  2085 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0888, 032a0838) -> 032a0838
  2086 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a08d8, 032a0888) -> 032a0888
  2087 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0928, 032a08d8) -> 032a08d8
  2088 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0978, 032a0928) -> 032a0928
  2089 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a09c8, 032a0978) -> 032a0978
  2090 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0a18, 032a09c8) -> 032a09c8
  2091 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0a68, 032a0a18) -> 032a0a18
  2092 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0ab8, 032a0a68) -> 032a0a68
  2093 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0b08, 032a0ab8) -> 032a0ab8
  2094 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0b58, 032a0b08) -> 032a0b08
  2095 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0ba8, 032a0b58) -> 032a0b58
  2096 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0bf8, 032a0ba8) -> 032a0ba8
  2097 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0c48, 032a0bf8) -> 032a0bf8
  2098 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0c98, 032a0c48) -> 032a0c48
  2099 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0ce8, 032a0c98) -> 032a0c98
  2100 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0d38, 032a0ce8) -> 032a0ce8
  2101 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0d88, 032a0d38) -> 032a0d38
  2102 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0dd8, 032a0d88) -> 032a0d88
  2103 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0e28, 032a0dd8) -> 032a0dd8
  2104 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0e78, 032a0e28) -> 032a0e28
  2105 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0ec8, 032a0e78) -> 032a0e78
  2106 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0f18, 032a0ec8) -> 032a0ec8
  2107 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0f68, 032a0f18) -> 032a0f18
  2108 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a0fb8, 032a0f68) -> 032a0f68
  2109 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1008, 032a0fb8) -> 032a0fb8
  2110 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1058, 032a1008) -> 032a1008
  2111 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a10a8, 032a1058) -> 032a1058
  2112 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a10f8, 032a10a8) -> 032a10a8
  2113 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1148, 032a10f8) -> 032a10f8
  2114 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1198, 032a1148) -> 032a1148
  2115 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a11e8, 032a1198) -> 032a1198
  2116 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1238, 032a11e8) -> 032a11e8
  2117 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1288, 032a1238) -> 032a1238
  2118 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a12d8, 032a1288) -> 032a1288
  2119 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1328, 032a12d8) -> 032a12d8
  2120 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1378, 032a1328) -> 032a1328
  2121 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a13c8, 032a1378) -> 032a1378
  2122 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1418, 032a13c8) -> 032a13c8
  2123 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1468, 032a1418) -> 032a1418
  2124 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a14b8, 032a1468) -> 032a1468
  2125 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1508, 032a14b8) -> 032a14b8
  2126 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1558, 032a1508) -> 032a1508
  2127 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a15a8, 032a1558) -> 032a1558
  2128 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a15f8, 032a15a8) -> 032a15a8
  2129 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1648, 032a15f8) -> 032a15f8
  2130 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1698, 032a1648) -> 032a1648
  2131 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a16e8, 032a1698) -> 032a1698
  2132 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1738, 032a16e8) -> 032a16e8
  2133 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1788, 032a1738) -> 032a1738
  2134 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a17d8, 032a1788) -> 032a1788
  2135 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1828, 032a17d8) -> 032a17d8
  2136 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1878, 032a1828) -> 032a1828
  2137 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a18c8, 032a1878) -> 032a1878
  2138 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1918, 032a18c8) -> 032a18c8
  2139 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1968, 032a1918) -> 032a1918
  2140 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a19b8, 032a1968) -> 032a1968
  2141 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1a08, 032a19b8) -> 032a19b8
  2142 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1a58, 032a1a08) -> 032a1a08
  2143 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1aa8, 032a1a58) -> 032a1a58
  2144 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1af8, 032a1aa8) -> 032a1aa8
  2145 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1b48, 032a1af8) -> 032a1af8
  2146 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1b98, 032a1b48) -> 032a1b48
  2147 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1be8, 032a1b98) -> 032a1b98
  2148 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1c38, 032a1be8) -> 032a1be8
  2149 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1c88, 032a1c38) -> 032a1c38
  2150 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1cd8, 032a1c88) -> 032a1c88
  2151 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1d28, 032a1cd8) -> 032a1cd8
  2152 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1d78, 032a1d28) -> 032a1d28
  2153 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1dc8, 032a1d78) -> 032a1d78
  2154 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1e18, 032a1dc8) -> 032a1dc8
  2155 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1e68, 032a1e18) -> 032a1e18
  2156 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1eb8, 032a1e68) -> 032a1e68
  2157 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1f08, 032a1eb8) -> 032a1eb8
  2158 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1f58, 032a1f08) -> 032a1f08
  2159 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1fa8, 032a1f58) -> 032a1f58
  2160 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a1ff8, 032a1fa8) -> 032a1fa8
  2161 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2048, 032a1ff8) -> 032a1ff8
  2162 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2098, 032a2048) -> 032a2048
  2163 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a20e8, 032a2098) -> 032a2098
  2164 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2138, 032a20e8) -> 032a20e8
  2165 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2188, 032a2138) -> 032a2138
  2166 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a21d8, 032a2188) -> 032a2188
  2167 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2228, 032a21d8) -> 032a21d8
  2168 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2278, 032a2228) -> 032a2228
  2169 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a22c8, 032a2278) -> 032a2278
  2170 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2318, 032a22c8) -> 032a22c8
  2171 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2368, 032a2318) -> 032a2318
  2172 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a23b8, 032a2368) -> 032a2368
  2173 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2408, 032a23b8) -> 032a23b8
  2174 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2458, 032a2408) -> 032a2408
  2175 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a24a8, 032a2458) -> 032a2458
  2176 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a24f8, 032a24a8) -> 032a24a8
  2177 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2548, 032a24f8) -> 032a24f8
  2178 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2598, 032a2548) -> 032a2548
  2179 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a25e8, 032a2598) -> 032a2598
  2180 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2638, 032a25e8) -> 032a25e8
  2181 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2688, 032a2638) -> 032a2638
  2182 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a26d8, 032a2688) -> 032a2688
  2183 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2728, 032a26d8) -> 032a26d8
  2184 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2778, 032a2728) -> 032a2728
  2185 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a27c8, 032a2778) -> 032a2778
  2186 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2818, 032a27c8) -> 032a27c8
  2187 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2868, 032a2818) -> 032a2818
  2188 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a28b8, 032a2868) -> 032a2868
  2189 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2908, 032a28b8) -> 032a28b8
  2190 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2958, 032a2908) -> 032a2908
  2191 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a29a8, 032a2958) -> 032a2958
  2192 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a29f8, 032a29a8) -> 032a29a8
  2193 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2a48, 032a29f8) -> 032a29f8
  2194 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2a98, 032a2a48) -> 032a2a48
  2195 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2ae8, 032a2a98) -> 032a2a98
  2196 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2b38, 032a2ae8) -> 032a2ae8
  2197 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2b88, 032a2b38) -> 032a2b38
  2198 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2bd8, 032a2b88) -> 032a2b88
  2199 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2c28, 032a2bd8) -> 032a2bd8
  2200 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2c78, 032a2c28) -> 032a2c28
  2201 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2cc8, 032a2c78) -> 032a2c78
  2202 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2d18, 032a2cc8) -> 032a2cc8
  2203 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2d68, 032a2d18) -> 032a2d18
  2204 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2db8, 032a2d68) -> 032a2d68
  2205 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2e08, 032a2db8) -> 032a2db8
  2206 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2e58, 032a2e08) -> 032a2e08
  2207 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2ea8, 032a2e58) -> 032a2e58
  2208 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2ef8, 032a2ea8) -> 032a2ea8
  2209 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2f48, 032a2ef8) -> 032a2ef8
  2210 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2f98, 032a2f48) -> 032a2f48
  2211 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a2fe8, 032a2f98) -> 032a2f98
  2212 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a3038, 032a2fe8) -> 032a2fe8
  2213 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a3088, 032a3038) -> 032a3038
  2214 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a30d8, 032a3088) -> 032a3088
  2215 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a3128, 032a30d8) -> 032a30d8
  2216 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a3178, 032a3128) -> 032a3128
  2217 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a31c8, 032a3178) -> 032a3178
  2218 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a3218, 032a31c8) -> 032a31c8
  2219 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a3268, 032a3218) -> 032a3218
  2220 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a32b8, 032a3268) -> 032a3268
  2221 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a3308, 032a32b8) -> 032a32b8
  2222 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a3358, 032a3308) -> 032a3308
  2223 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a33a8, 032a3358) -> 032a3358
  2224 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a33f8, 032a33a8) -> 032a33a8
  2225 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a3448, 032a33f8) -> 032a33f8
  2226 00f4 009BB7D9 kernel32.InterlockedCompareExchange(024a1d00, 032a3498, 032a3448) -> 032a3448
```
