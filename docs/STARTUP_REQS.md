# Start-up requirements: entry point to window and D3D9 device

Source: static reading of `orig.nosync/lhp_win32.exe` with capstone (scripts in the job tmp folder `st/`). Nothing from the game was run. Ghidra was not used (locked).
All addresses are guest VAs. "fn X @ Y" means call site Y inside function X.
Order inside one function is address order. For straight-line init code this is execution order; branches can change it. Items marked **OPEN** were not settled statically; each names the run-time evidence that settles it.

## Summary

| Item | Value |
|---|---|
| Entry | 0x00A69393: `call __security_init_cookie (0xA771FE)`, then `jmp __tmainCRTStartup (0xA691B3)` |
| WinMain | **0x00A217A0**, called at 0xA69325 as `WinMain(0x400000, 0, lpCmdLine, nShowCmd)` |
| C++ static initialisers | `__xc_a`=0xC43000 .. `__xc_z`=0xC43860: 536 slots, **284 non-null**, all in .text |
| C initialisers | `__xi_a`=0xC43964 .. `__xi_z`=0xC43C84: 200 slots, **6 non-null** |
| Hardware init (`NuInitHardware`) | 0xA3CA00, called from 0x6A6C20 @ 0x6A6E12 |
| Window class / WndProc | "TTalesWindow", WndProc **0x009EAF50**, created in 0x9EB2F0 |
| Message pump | 0x9D42E0 (PeekMessageA loop) |
| D3D object | 0x10B0ED0 (`IDirect3D9*` at +0, `IDirect3DDevice9*` at +4 = 0x10B0ED4) |
| CreateDevice | 0xA37D90 @ 0xA38272 |

## 1. CRT start-up (0xA69393 to WinMain)

MSVC 2005 `crt0.c` shape. Import calls in order:

| # | Call site | Function | Import | Purpose |
|---|---|---|---|---|
| 1 | 0xA77233 | 0xA771FE `__security_init_cookie` | GetSystemTimeAsFileTime | /GS cookie |
| 2 | 0xA7723F | 0xA771FE | GetCurrentProcessId | cookie |
| 3 | 0xA77247 | 0xA771FE | GetCurrentThreadId | cookie |
| 4 | 0xA7724F | 0xA771FE | GetTickCount | cookie |
| 5 | 0xA7725B | 0xA771FE | QueryPerformanceCounter | cookie |
| 6 | 0xA691C7 | 0xA691B3 `__tmainCRTStartup` | GetStartupInfoA | nShowCmd source |
| 7 | 0xA691E2 | 0xA691B3 | GetProcessHeap | OSVERSIONINFO buffer |
| 8 | 0xA691E5 | 0xA691B3 | HeapAlloc | " |
| 9 | 0xA69201 | 0xA691B3 | GetVersionExA | `_osplatform`, `_winmajor`; selects heap type |
| 10 | 0xA6920E | 0xA691B3 | GetProcessHeap | free buffer |
| 11 | 0xA69211 | 0xA691B3 | HeapFree | " |
| 12 | 0xA7713B | 0xA7712A `_heap_init` | HeapCreate | CRT heap. `__heap_select` (0xA770CF) uses the system heap when GetVersionExA reports NT and major >= 5; otherwise `__sbh_heap_init` (0xA79A81) runs |
| 13 | 0xA73E74 | 0xA73E6E `_mtinit` | GetModuleHandleA("KERNEL32.DLL") | |
| 14-17 | 0xA73E96, 0xA73EA3, 0xA73EB0, 0xA73EBD | 0xA73E6E | GetProcAddress FlsAlloc, FlsGetValue, FlsSetValue, FlsFree | NULL results make the CRT fall back to TLS |
| 18 | 0xA73F0D | 0xA73E6E | TlsAlloc | |
| 19 | 0xA73F28 | 0xA73E6E | TlsSetValue | |
| 20+ | 0xA73A06/0xA73A32/0xA73A4D | 0xA739F9 `_encode_pointer` (called at 0xA73F3D..0xA73F6D) | TlsGetValue, GetModuleHandleA("KERNEL32.DLL"), GetProcAddress("EncodePointer") | per call |
| | 0xA8010C, 0xA8011C | 0xA800C3 (via `_mtinitlocks` 0xA7981F) | GetModuleHandleA("kernel32.dll"), GetProcAddress("InitializeCriticalSectionAndSpinCount") | locks |
| | 0xA73FD7 | 0xA73E6E | GetCurrentThreadId | `_initptd` |
| | 0xA75C54, 0xA75D7E, 0xA75E08, 0xA75E1A, 0xA75E72 | 0xA75C3F `_ioinit` | GetStartupInfoA, GetFileType, GetStdHandle, GetFileType, SetHandleCount | stdio handles |
| | 0xA692C0 | 0xA691B3 | GetCommandLineA | |
| | 0xA76F6E..0xA77078 | 0xA76F52 `__crtGetEnvironmentStringsA` | GetEnvironmentStringsW, WideCharToMultiByte, FreeEnvironmentStringsW (A fallback: GetEnvironmentStrings, FreeEnvironmentStringsA) | environment |
| | 0xA76EC3 | 0xA76E99 `_setargv` | GetModuleFileNameA | argv[0] |
| | - | 0xA76C1C `_setenvp` | none | |
| | - | 0xA6D301 `_cinit` | see below | |
| | - | 0xA76BBF `_wincmdln` | none | lpCmdLine |

`_cinit` (0xA6D301):
- 0xA6D31D: calls `_FPinit` through [0xD1FBC0] if non-null and in the image.
- 0xA6D333: `_initterm_e(__xi_a=0xC43964, __xi_z=0xC43C84)`. 6 entries: 0xA68198, 0xA70975, 0xA741C2, 0xA80B19 (multibyte code page: GetACP, GetCPInfo, GetOEMCP), 0xA87F28 (SSE2 detection; `cpuid` at 0xA87EF0, 0xA87F03 in 0xA87EC8), 0xA76A05 (SetUnhandledExceptionFilter).
- 0xA6D345: `atexit(0xA770AB)`.
- 0xA6D35B..0xA6D368: inline `_initterm` loop over `__xc_a`=0xC43000 .. `__xc_z`=0xC43860. 284 non-null entries. Most only build game tables (strings such as `ability_FLIPENDO`, `DefaultWeapSmall`, `job_occlusion`). The ones that reach non-CRT imports (walk depth 5):
  - 0xC3E8C0: registry read `HKCU\SOFTWARE\<game key>` value `Hires` through 0x943B60 (RegOpenKeyExA @ 0x943D22, RegQueryValueExA @ 0x943D4A, RegCloseKey @ 0x943D6C). **This runs before WinMain.**
  - 0xC3E3E0: CreateSemaphoreA x4 (0x971F06, 0x971F15, 0x971F22, 0x971F2F), ReleaseSemaphore @ 0x971F6A.
  - 0xC3E410, 0xC3E430: InterlockedCompareExchange @ 0x970AE7.
  - Many reach HeapAlloc/InitializeCriticalSection/RaiseException (operator new and its failure path).
- 0xA6D38A: TLS callback through [0x10DD0E0] if non-null. **OPEN:** read [0x10DD0E0] after load.

nShowCmd = `STARTUPINFO.wShowWindow` if `STARTF_USESHOWWINDOW`, else 10 (SW_SHOWDEFAULT) (0xA6930E..0xA6931C). After WinMain: `exit` (0xA6D475) or `_cexit` (0xA6D497).

## 2. WinMain to the first window

WinMain 0xA217A0, in order:

| Call site | Call | Notes |
|---|---|---|
| 0xA217D0, 0xA217DD, 0xA217EA | SystemParametersInfoA 0x3A, 0x34, 0x32 | SPI_GETSTICKYKEYS / GETTOGGLEKEYS / GETFILTERKEYS into 0xED4088, 0xED4090, 0xED4098 |
| 0xA217EE | 0x944E90(0) | 6 more SystemParametersInfoA (0x944EAC..0x944F6F): disables the accessibility hot keys |
| 0xA21801 | SystemParametersInfoA 0x10 | SPI_GETSCREENSAVEACTIVE |
| 0xA2180B | SystemParametersInfoA 0x11 (0) | SPI_SETSCREENSAVEACTIVE off |
| 0xA21812 | SetThreadExecutionState(0x80000003) | |
| 0xA21818 | 0x93CB90 | CPU detection: `cpuid` leaf 0 (0x93CC13), leaf 1 (0x93CCB0; bits tested with shr 0xC, 0x19), 0x80000000/0x80000001 (0x93CE43..0x93CE8E; bit 31 = 3DNow) |
| 0xA21821 | CoInitializeEx(0, COINIT_APARTMENTTHREADED) | |
| 0xA21832 | 0x9703D0(lpCmdLine) | command line, below |
| 0xA21839 | 0xA21630(1) | permission check, below. Returns false -> skips the game |
| 0xA21849, 0xA21850 | GetCurrentThread, SetThreadAffinityMask(1) | main thread pinned to CPU 0 |
| 0xA21862 | 0x6A7500 | game main. 0x6A7542 -> 0x6A6C20 -> 0xA3CA00 (`NuInitHardware`) @ 0x6A6E12 |
| 0xA2186A | 0x9D4220 | shutdown |

**Command line (0x9703D0).** GetModuleHandleA @ 0x9703E9, GetModuleFileNameA @ 0x9703F0. Flag strings compared (0x963940 is the compare): `-RelaunchMCE` (0x9704D4), `-NoFocus_Pause` (0x9704F7), `-NoFocus_Process` (0x970513), `BackgroundPadInput` (0x970534), `/timeout:` (0x97054A, 0x97055C), `-Windowed` (0x9705C2), `-DumpShaders` (0x9705E3), `-HiresScreenshot` (0x9705F9), `-Fullscreen` (0x97060B), `-openautomate` (0x970629). 0x98C6E0 (from 0xA3CAD3) also compares `TMSERVER` and `SOAKTEST`.

**Permission check (0xA21630).** CoInitialize @ 0xA21665, CoCreateInstance @ 0xA21685 (CLSID at 0xD1C528, IID at 0xD1C538), MultiByteToWideChar @ 0xA216CC, SysAllocString @ 0xA216D7, a method call @ 0xA216EF, SysFreeString @ 0xA216F8, CoUninitialize @ 0xA2172D. On denial: MessageBoxA "Insufficient Permissions" @ 0xA21782. This matches the Vista Games Explorer parental-control check (`IGameExplorer::VerifyAccess`); **OPEN:** decode the GUIDs at 0xD1C528/0xD1C538, and confirm that a failed CoCreateInstance (jl at 0xA21691 -> 0xA21712) leads to "allowed". Run-time check: return REGDB_E_CLASSNOTREG and watch whether the MessageBoxA at 0xA21782 is reached.

**NuInitHardware (0xA3CA00).** It parses a tag list built by 0x6A6C20 (pushes 0x6A6CB9..0x6A6E0A; dispatch at 0xA3CB80, table 0xA3D890, tags 0..0x5E). In address order:

| Call site | Callee | Imports / effect |
|---|---|---|
| 0xA3CAD3 | 0x98C6E0 | `TMSERVER`, `SOAKTEST` flags |
| 0xA3CAE2 | operator new(4) | object with vtable 0xD19388 stored at 0x1043358 (only if [0x10D4C3E]==0). Its methods are polled by the pump and by 0xA37E9E |
| 0xA3CB4A, 0xA3D1F8 | InitializeCriticalSection | |
| 0xA3D21E, 0xA3D259 | TlsAlloc, TlsSetValue | thread-local "Main" context |
| 0xA3D266 | 0x9D28D0 | job queues "Main", "Render", "Render Stream" |
| 0xA3D26B | 0xA3B490 | **starts the Render thread** (section 5), CreateEventA "renderThread_wakeup" @ 0xA3B4E2, ResumeThread @ 0xA3B4FD |
| 0xA3D275 | 0x93C3E0 | **Winsock:** WSAStartup @ 0x93C3F8, gethostname @ 0x93C44A, gethostbyname @ 0x93C455, inet_ntoa @ 0x93C467, WSACleanup @ 0x93C49B |
| 0xA3D29C | 0x9457E0 | |
| 0xA3D2A9 | 0xA2C7E0 | **Direct3DCreate9** (section 4) |
| 0xA3D2B3 | 0x9F3FA0 | file-system init; platform suffix table (`_pc.pak`, `_lr_pc.gsc` ...) in 0x928B50; **starts m_readThread and m_decodeThread** via 0x9B2610 |
| 0xA3D2BB, 0xA3D2C5 | 0x9A7320, 0x9A7860 | InitializeCriticalSection, CreateSemaphoreA x2 |
| 0xA3D2CA | 0x92A020 | **timer calibration:** GetProcessAffinityMask @ 0x92A045, SetProcessAffinityMask @ 0x92A05D, QueryPerformanceCounter @ 0x92A064, QueryPerformanceFrequency @ 0x92A06F, SetProcessAffinityMask (restore) @ 0x92A07D |
| 0xA3D2EA | 0x96F6D0 | GetModuleFileNameA @ 0x96F705; profile folder via 0x945250: SHGetFolderPathA @ 0x9452A1 with CSIDL 0x1A (APPDATA) or 5 (PERSONAL) chosen at 0x945288/0x94528F, GetFileAttributesA @ 0x945386, SHCreateDirectoryExA @ 0x9453E1 (also contains the dev path `O:\PROFILE\`); file name `pcconfig.txt` (0x96F77C); then HKCU `Hires` read via 0x943B60 |
| 0xA3D2F4 | 0x9F7420 | reads `pcconfig.txt` (0x9F748C); writes it through 0x943E20 with fopen mode "w" (0xD19348): FileVersion, ScreenWidth/Height, WindowWidth/Height/Left/Top, ScreenRefreshRate, VerticalSync, FSAA, AspectRatio, TextureQuality, SoftParticles, ForceMultithreadedD3D, IgnoreVendorPresets, Brightness, volumes, MouseSensitivity, InvertY ...; HKCU `Hires` again (RegOpenKeyExA @ 0x9F7596, RegQueryValueExA @ 0x9F75C0) |
| 0xA3D304 | 0x98E130("__DISC__") | |
| 0xA3D319 | 0xA048A0 | DAT file system: `upd:\%sPATCH.DAT`, `%sPATCH%d.DAT`, `GAME.DAT` (0xA048DC..0xA04960). Open/read of the DAT files goes through the CreateFileA wrapper 0x98FF60 (@ 0x990016) |
| 0xA3D3B1 | 0x96FD80 | `HKLM\SOFTWARE\<game key>` (KEY_QUERY_VALUE) value `LanguageVal` (RegOpenKeyExA @ 0x96FDF1, RegQueryValueExA @ 0x96FE13); fallback GetUserDefaultLangID @ 0x96FE2A, GetUserGeoID @ 0x96FF85 |
| 0xA3D3B9 | 0xA2C8A0 | **Shader Model 2 check**; exits with "Sorry, but your machine is unable to run this game..." (0xA2CB51) |
| 0xA3D3F0 | 0x9EAF10 | **disc check**, only if tag 0x34 was passed (flag at [esp+0x80], set at 0xA3D0CF) |
| 0xA3D598 | 0x9EB2F0 | **window** (section 3) |
| 0xA3D5B2 | 0x945090 | CreateMutexA @ 0x9450D0, ShowWindow @ 0x945115/0x945141 (**OPEN:** not read in detail; likely single-instance) |
| 0xA3D5CF | 0x9BFD20 | **starts TextureDecode thread** |
| 0xA3D5D4 | 0xA3A3E0 | 0xA2C8A0 @ 0xA3A3E2, then 0xA3A150 -> 0xA37D90 @ 0xA3A30E: **CreateDevice** |
| 0xA3D627 | SetCursor | |
| 0xA3D682 | 0xA38B20 | shader search paths `C:\genShaders`, `./shaders;../nu2api/shaderBuilder;...` |

Registry key text: the strings `WB Games\LEGOHarryPotter` (0xC8C234, pointer at 0xE89F18) and `WB Games` (0xC8C264, pointer at 0xE89F10) are appended to `SOFTWARE\` (0xD18DFC). **OPEN:** which pointer each reader uses; confirm by logging the RegOpenKeyExA path. Root keys read from the code: `Hires` uses HKCU (push 0x80000001 @ 0x943D1D); `LanguageVal` uses HKLM (push 0x80000002 @ 0x96FDEC). **The string `PathVal` does not occur in the executable.** The game finds its files relative to its own folder (GetModuleFileNameA in 0x9703D0 and 0x96F6D0).

**Disc check (0x9982E0, from 0x9EAF10).** GetLogicalDrives @ 0x9982F2; for each drive, GetDriveTypeA == DRIVE_CDROM, GetVolumeInformationA, file system name "UDF", then looks for `%ssetup\game\%s` with size > 0. On failure: MessageBoxW @ 0x9EAF36 and exit. The tag list from 0x6A6C20 does not push tag 0x34, so static reading says the check is skipped. **OPEN:** confirm that 0x9EAF10 is never called (breakpoint/log).

CPU feature users: 0x93CB90 (from WinMain), 0xA87EC8 (CRT SSE2). A `cpuid` byte match in 0x96F6D0 at 0x96F7C3 does not decode on an instruction boundary in context; treat it as a false hit. No `rdtsc` in any function.

## 3. Window and message loop

Window creation 0x9EB2F0 (args: hInstance, nCmdShow, icon id):
- LoadIconA(hInstance, id) @ 0x9EB301; LoadCursorA(NULL, IDC_ARROW) @ 0x9EB372.
- WNDCLASSEXA at 0xFF1C44: cbSize 0x30, style CS_HREDRAW|CS_VREDRAW (3), lpfnWndProc **0x9EAF50**, hInstance, hIcon, hCursor, hbrBackground 6 (COLOR_WINDOW+1), no menu, class `TTalesWindow` (0xD1BD94), hIconSm = hIcon. RegisterClassExA @ 0x9EB39C.
- Rect: left/top/width/height from 0xED88A4..0xED88B0 (pcconfig values); a negative left/top centres the window using the screen size at 0x10B14E0/0x10B14E4. AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW 0xCF0000, FALSE) @ 0x9EB41D.
- CreateWindowExA(0, "TTalesWindow", title [0xED4060], WS_OVERLAPPEDWINDOW, x, y, w, h, NULL, NULL, hInstance, NULL) @ 0x9EB459.
- 0x945090(hwnd, nCmdShow); if it returns 1: ShowWindow @ 0x9EB49B, UpdateWindow @ 0x9EB4A2.
- SendMessageA WM_SETICON (0x80) ICON_BIG and ICON_SMALL @ 0x9EB4B7, 0x9EB4C1.
- EnumDisplayMonitors(NULL, NULL, 0x945080, 0) @ 0x9EB4D1 counts monitors. If more than one monitor (and two flags): LoadLibraryA("dwmapi.dll") @ 0x9EB4F9, GetProcAddress("DwmEnableComposition") @ 0x9EB50E, call with FALSE.
- HWND is stored at 0x104336C (used as D3D focus/device window).

Device path 0xA37D90 also calls GetWindowLongA @ 0xA37F1D, AdjustWindowRect @ 0xA37F29, MoveWindow @ 0xA37F86, SetWindowPos @ 0xA37F9E in windowed mode, and SendMessageA @ 0xA383FC/0xA38A40.

WndProc 0x9EAF50 first calls each registered hook (array 0xFC0C98, count 0x104334C); a hook can consume the message. Then:

| Message | Handler | Effect |
|---|---|---|
| WM_DESTROY 0x2 | 0x9EB025 | sets 0x1043360=1, PostQuitMessage @ 0x9EB031 |
| WM_MOVE 0x3 | 0x9EAFDC | stores x,y at 0x1043330/0x1043334 |
| WM_SIZE 0x5 | 0x9EAFF6 | stores w,h (0xED407C/0xED4080, 0xED88A4/0xED88A8), calls 0x93E6A0 on the D3D object (device reset path) |
| WM_ACTIVATE 0x6, WM_ACTIVATEAPP 0x1C | 0x9EB084, 0x9EB080 | focus flag 0xED4058 |
| WM_KILLFOCUS 0x8 | 0x9EAFC9 | focus flag 0xED4058 = 0 |
| WM_CLOSE 0x10 | 0x9EB040 | shutdown 0x9D4220, exit(0) |
| WM_SETCURSOR 0x20 | 0x9EB0A0 | SetCursor @ 0x9EB0AB unless [0x10B102C]==1 |
| WM_KEYDOWN 0x100 / WM_KEYUP 0x101 | 0x9EB0E2 / 0x9EB0BD | scan code from lParam into key table 0x10431A0 |
| WM_SYSKEYDOWN 0x104 | 0x9EB0B1 | returns 1 (swallows Alt) |
| WM_MOUSEMOVE 0x200 | 0x9EB179 | normalised mouse to 0x1043328/0x104332C |
| WM_DEVICECHANGE 0x219 | 0x9EB149 | DBT_DEVICEARRIVAL (0x8000) sets 0xED7F44=1 (pad rescan) |
| WM_IME_SETCONTEXT 0x281 | 0x9EB12F | DefWindowProcA with modified lParam |
| others | 0x9EB11A | DefWindowProcA @ 0x9EB122 |

Message pump 0x9D42E0 (called from 0xA3B5A0 @ 0xA3B661, 0xA3B750, 0xA3B83C, and from shutdown): `while (PeekMessageA(&msg 0xFF2B38, NULL, 0, 0, PM_REMOVE)) { TranslateMessage; DispatchMessageA; if (msg.message == WM_QUIT) return; }` (0x9D42F6..0x9D4330). Then it polls the object at 0x1043358 (vtable +4, +8 @ 0x9D4341, 0x9D434E); if +8 returns true: shutdown and exit(0). No GetMessage loop. The UI slice must deliver messages to 0x9EAF50 synchronously inside DispatchMessageA, and must generate WM_SIZE/WM_MOVE/WM_ACTIVATEAPP on window creation like Windows does.

## 4. Direct3D 9 start-up

D3D object at 0x10B0ED0: +0 `IDirect3D9*`, +4 `IDirect3DDevice9*`, +0xC D3DCAPS9, +0x13C D3DPRESENT_PARAMETERS, +0x1A8 D3DADAPTER_IDENTIFIER9, +0x610 D3DDISPLAYMODE, +0x624 mode table (16-byte entries: width, height, refresh, format).

Vtable offsets use the published IDirect3D9 order (0x10 GetAdapterCount ... 0x40 CreateDevice). Calls in boot order:

| Call site | Method (offset) | Arguments |
|---|---|---|
| 0xA2C7E6 | Direct3DCreate9(32) | through thunk 0xA960D2 (`jmp [0x10DED98]`); NULL -> "Failed to initialize DirectX" fatal |
| 0xA2C810 | GetDeviceCaps (0x38) | (0, HAL, +0xC) |
| 0xA2C825 | GetAdapterIdentifier (0x14) | (0, 0, +0x1A8) |
| 0xA2C838 | GetAdapterDisplayMode (0x20) | (0, +0x610) |
| 0xA2C846 | 0x93DA80(w, h) | finds the desktop mode in the mode table (index at +0x62C). **OPEN:** the function that fills the mode table (GetAdapterModeCount/EnumAdapterModes, 0x18/0x1C) was not found statically; log IDirect3D9 calls at run time |
| 0xA3D3B9, 0xA3A3E2 | 0xA2C8A0 | caps check, Shader Model 2 required |
| 0xA37E39 | GetDeviceCaps (0x38) | (0, HAL, +0xC) |
| 0xA37FD0 | CheckDeviceType (0x24) | (0, HAL, display format, D3DFMT_A8R8G8B8 0x15, Windowed) |
| 0xA380B1 | GetAdapterIdentifier (0x14) | (0, 0, +0x1A8) |
| 0xA380FD, 0xA38134 | CheckDeviceMultiSampleType (0x2C) | back-buffer format and D24S8, looping over the MSAA table 0xE9BEB8 |
| 0xA3821F | 0x93DEE0 | CheckDeviceFormat (0x28) @ 0x93DFA1, 0x93E031, 0x93E143 |
| 0xA38272 | **CreateDevice (0x40)** | (Adapter [+0x1A4]=0, DeviceType [+0x5F4], hFocusWindow [+0x158], BehaviorFlags [+0x5F8], &pp 0x10B100C, &dev 0x10B0ED4) |
| 0xA382BA | CreateDevice retry | up to 5 retries with Sleep(500) @ 0xA3828B |
| 0xA3830F | CreateDevice fallback | 640x480, 60 Hz |
| 0xA38331 | failure | "Failed to create d3d device!"; HRESULTs tested: 0x8876017C, 0x88760868, 0x8876086A |

D3DPRESENT_PARAMETERS (object +0x13C), set in 0xA37E54..0xA38233:

| Field | Value |
|---|---|
| BackBufferWidth/Height (+0x13C/+0x140) | mode table entry (0xA38015, 0xA3801E); 640x480 in the fallback |
| BackBufferFormat (+0x144) | D3DFMT_A8R8G8B8 if CheckDeviceType succeeds, else the display format |
| BackBufferCount (+0x148) | 1; 2 when vsync (0xA37EC0) |
| MultiSampleType/Quality (+0x14C/+0x150) | from table 0xE9BEB8 and the CheckDeviceMultiSampleType result |
| SwapEffect (+0x154) | 1, D3DSWAPEFFECT_DISCARD |
| hDeviceWindow (+0x158) | [0x104336C] |
| Windowed (+0x15C) | byte [obj+0x668] |
| EnableAutoDepthStencil (+0x160) | 1 (may be cleared at 0xA38233) |
| AutoDepthStencilFormat (+0x164) | 0x4B, D3DFMT_D24S8 |
| Flags (+0x168) | 2, DISCARDDEPTHSTENCIL (may be cleared at 0xA3822C) |
| FullScreen_RefreshRateInHz (+0x16C) | mode table refresh; 0 when windowed |
| PresentationInterval (+0x170) | D3DPRESENT_INTERVAL_ONE with vsync, else IMMEDIATE 0x80000000 |

BehaviorFlags (+0x5F8): PUREDEVICE (0x10) unless [obj+0x5FC]; MULTITHREADED (4) if [0xED88C8] (ForceMultithreadedD3D); HARDWARE_VERTEXPROCESSING (0x40) if caps VertexShaderVersion >= 0xFFFE0101; else MIXED (0x80) if DevCaps has HWTRANSFORMANDLIGHT; else SOFTWARE (0x20). DeviceType is the value of ebx in 0xA37D90 (also pushed as the GetDeviceCaps device type); read as 1 = HAL. **OPEN:** confirm ebx = 1 by logging CreateDevice arguments.

## 5. Threads started during boot

All named threads go through the wrapper 0x98FD20: CreateSemaphoreA @ 0x98FD9E, CreateThread @ 0x98FDCD, SetThreadPriority @ 0x98FDDE, 0x929E30 (calls RaiseException @ 0x929E7D; most likely the MSVC thread-name exception 0x406D1388; **OPEN:** read the exception code there), ResumeThread @ 0x98FE0E.

Started inside NuInitHardware (boot):

| Thread | Start routine | Started at | Purpose |
|---|---|---|---|
| Render | 0xA3B3D0 | 0xA3B490 @ 0xA3B4CD (from 0xA3D26B) | render worker; woken by event "renderThread_wakeup" |
| m_readThread | 0x98EA30 | 0x9B2610 @ 0x9B26AF (from 0x9F3FA0 @ 0xA3D2B3) | file reads (streaming) |
| m_decodeThread | 0x9A6CC0 | 0x9B2610 @ 0x9B26E9 | decompression of read data |
| TextureDecode | not recovered | 0x9BFD20 @ 0x9BFE0E (from 0xA3D5CF) | texture decode |

Other thread starters (not shown on the boot path; **OPEN** whether they start before the first frame): SaveCore 0x962EB0 (0x9A6010), StreamClientThread 0x9A3920 (0x9B1480), MusicStream 0x9B1A20 (0x9C1030), bgprocThread 0x9A7D10 (0x9C22E0), NuFmvStream 0x991B30 (0x9C2680), SoundEvent 0xA37730 (0xA378F0), unnamed in 0x9A5E00, 0x9A5E50, 0x9E4AC0 (start 0x98ED70). Direct CreateThread @ 0x9188D2 (0x918830), start routines 0x95CBD0 (via 0x95DB80) and 0x95DC40 (via 0x95E6B0); purpose not determined. Run-time evidence: log every CreateThread start address up to the first Present.

## 6. Expected host failures

1. **Thread naming.** Every named thread calls 0x929E30 -> RaiseException. If this is the 0x406D1388 debugger exception, the host must deliver it to the guest SEH frame (handler continues) or skip it; unhandled, it kills the process at the first thread start (Render, 0xA3D26B).
2. **Permission check (0xA21630).** CoCreateInstance of a Games Explorer class. If the failure branch is not "allowed", WinMain shows "Insufficient Permissions" and never starts the game.
3. **Winsock in boot (0x93C3E0).** WSAStartup, gethostname, gethostbyname, inet_ntoa run unconditionally in NuInitHardware. gethostbyname on a macOS host name can block for seconds or fail; return the loopback address quickly.
4. **D3D9 requirements.** Shader Model 2 caps check (0xA2C8A0) exits with a message box. CreateDevice needs D24S8, A8R8G8B8, PUREDEVICE + HARDWARE_VP accepted, MSAA queries answered, and an IDirect3D9 mode table (the code indexes +0x624 with the display-mode index). A failing CreateDevice costs 2.5 s of retries before the fallback.
5. **Timing and affinity.** SetThreadAffinityMask(1) in WinMain and Get/SetProcessAffinityMask around QueryPerformanceFrequency (0x92A020). The host must return a non-zero affinity mask and a stable QPC frequency.

Other items the host must answer before the window appears:
- GetVersionExA must report NT 5.x or later; otherwise the CRT selects the old small-block heap.
- HKCU/HKLM registry reads (pre-WinMain `Hires` in initialiser 0xC3E8C0; `LanguageVal`) must return a clean "not found" or the installer values.
- SHGetFolderPathA (CSIDL 0x1A or 5), SHCreateDirectoryExA and a writable `pcconfig.txt` in that folder.
- SystemParametersInfoA accessibility calls and SetThreadExecutionState: accept and return success.
- EnumDisplayMonitors must call its callback; more than one monitor triggers LoadLibraryA("dwmapi.dll").
- No Steam API strings (`steam_api`, `SteamAPI`) and no `rdtsc`. The only CD check (0x9982E0) is gated by tag 0x34, which the boot tag list does not pass.
- Dev-only paths exist (`10.135.72.60` "Target Manager filesystem" in 0x9E4CE0, `O:\PROFILE\`, `C:\genShaders`). **OPEN:** whether any is touched on a retail boot.
