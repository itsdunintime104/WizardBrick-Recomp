# Win32 API coverage: golden trace vs our runtime

Source: `~/Developer/hp-wine-oracle/trace/golden.jsonl` (the original exe under Wine, first 29.5 s, `notes/golden_trace.md`). Our status is read from the runtime export tables in the working tree on 2026-10-24 (HEAD 39160c0 plus an uncommitted `runtime/src/win32_core.c` edit from another lane that adds Fls*, EncodePointer, DecodePointer).

Rerun (about 10 s):

```
python -I tools/trace/api_coverage.py ~/Developer/hp-wine-oracle/trace/golden.jsonl --src runtime/src --imports notes/win32_imports.md
```

## Result

- 107 distinct Win32 APIs called (110 rows minus 3 `+d3d9` COM events). Status: impl 90, fixed 10, partial 5, stub 0, fallback 0, missing 2.
- No call reaches a generated fallback stub (`win32_unsupported.c`) or a slice stub (`D3DX_UNREACHED`, `CreatePipe`, ...).
- The 2 missing APIs (and, at HEAD, 6 more) are reached only through GetProcAddress, where ours returns 0 and the MSVC 2005 CRT takes its fallback path.
- No class (c) gap: nothing the original calls aborts in our runtime. The partial rows abort or report only on inputs the original never passes (notes below).
- COM methods are not measured. Wine does not relay vtable calls; the `+d3d9` channel shows only CreateDevice, Present and BeginScene. IDirect3DDevice9, IDirectInput8, IXAudio2, IGameExplorer and IWbem* method coverage needs our own COM trace (`notes/golden_trace.md`, logging spec item 4).
- Wine never relays GetCurrentThreadId, GetCurrentProcessId, GetCurrentProcess, GetCurrentThread. Ours: impl, impl, fixed, fixed.

Status values (from `tools/trace/api_coverage.py`): impl = slice entry that does work; partial = slice entry that reports UNSUPPORTED on some inputs; fixed = returns a constant, reads no argument; stub = always UNSUPPORTED or abort; fallback = generated stub (returns 0); missing = not exported and not in the inventory (GetProcAddress returns 0).

Class: (a) harmless as is; (b) wrong answer that changes the game's path; (c) missing and will abort; `-` no gap; `?` cannot be decided from this trace.

## Gaps in fix order

There are 9 gaps, not 15. Ranked by effect on the path first, then by first seq.

| # | API | first seq | count | class | Fix |
|---|---|---|---|---|---|
| 1 | CoCreateInstance(CLSID_GameExplorer {9A5EA990-...}) @0xA2168B | 1776 | 1 | b | Wine S_OK, then SysAllocString(exe path) and IGameExplorer::VerifyAccess. Ours REGDB_E_CLASSNOTREG on purpose (`win32_com.c:48`, absent_on_xp). Confirm the jl at 0xA21691 -> 0xA21712 means "allowed" (`docs/STARTUP_REQS.md:88` OPEN); if not, register a fake class whose VerifyAccess grants access. |
| 2 | CoCreateInstance(CLSID_WbemLocator) @0x941EFB | 5225 | 4 | b | Wine S_OK, then CoSetProxyBlanket and a WMI query of `\\.\root\cimv2` Win32_PNPEntity DeviceID (XInput-device detection). Ours REGDB_E_CLASSNOTREG and an UNSUPPORTED line per CLSID. Check what 0x941EFB's caller does on failure; with no pad the result is probably the same. Either confirm, or register a locator whose query returns an empty enumerator. |
| 3 | D3DXCompileShader | 275598 | 12 | ? | Wine S_OK for all 12 (vs_3_0 vertexProgram, ps_3_0 fragmentProgram, flags 0x200, bgprocThread). Ours compiles with vkd3d (`d3dx_shader.c:1465`, D3DXERR_INVALIDDATA on failure). Feed the same 12 sources through our thunk and expect S_OK. |
| 4 | SetThreadExecutionState(0x80000003) @0xA21818 | 1771 | 1 | a? | Ours returns ES_CONTINUOUS 0x80000000 (`win32_sync.c`, imp_SetThreadExecutionState); Wine 7. Check whether the caller uses the value; returning the previous state is cheap. |
| 5 | GetProcAddress(kernel32.dll, InitializeCriticalSectionAndSpinCount) @0xA80122 | 264 | 1 (+20 calls) | a | Ours 0; CRT uses InitializeCriticalSection. Export it from the sync slice so compare.py stays aligned with Wine. |
| 6 | GetProcAddress(KERNEL32, IsProcessorFeaturePresent), arg 0 | 672 | 1 | a | Ours 0; CRT runs its FDIV test in lifted x87 instead. Export it (return 0 for PF 0, as Wine) to remove the dependence on the FDIV test. |
| 7 | GetProcAddress Fls*, EncodePointer, DecodePointer | 234-261 | 38 (+683 calls) | a | Missing at HEAD 39160c0; exported in the uncommitted `win32_core.c` edit. The owning lane commits it. |
| 8 | TranslateMessage | 268866 | 13 | a | Ours always 1; Wine 0 for the first message. Return it from the message kind if a caller checks it. |
| 9 | SystemParametersInfoA SPI_SET{FILTER,TOGGLE,STICKY}KEYS @0xA217D2 area | 1763 | 3 | a | Handled; Wine returns 0, ours returns `ok` from the switch (not checked whether TRUE). No action unless a caller checks it. |

## All calls, by first seq

Totals: com 3, fixed 10, impl 90, missing 2, partial 5, APIs 110 (the 3 `com` rows are `+d3d9` channel events). The script output also gives threads, our function and Wine return values.

| DLL!Func | count | first seq | first t (s) | status | class | note |
|---|---|---|---|---|---|---|
| kernel32!GetSystemTimeAsFileTime | 1 | 223 | 0.000 | impl | - |  |
| kernel32!GetTickCount | 1 | 224 | 0.000 | impl | - |  |
| kernel32!QueryPerformanceCounter | 4820 | 225 | 0.000 | impl | - |  |
| kernel32!GetStartupInfoA | 2 | 226 | 0.000 | impl | - |  |
| kernel32!GetProcessHeap | 2 | 227 | 0.000 | impl | - |  |
| ntdll!HeapAlloc | 813 | 228 | 0.000 | impl | - |  |
| kernel32!GetVersionExA | 1 | 229 | 0.000 | impl | - |  |
| kernel32!HeapFree | 182 | 231 | 0.000 | impl | - |  |
| kernel32!HeapCreate | 1 | 232 | 0.000 | impl | - |  |
| kernel32!GetModuleHandleA | 35 | 233 | 0.000 | impl | - |  |
| kernel32!GetProcAddress | 40 | 234 | 0.000 | impl | - |  |
| kernel32!TlsAlloc | 2 | 238 | 0.000 | impl | - |  |
| ntdll!EncodePointer | 338 | 241 | 0.000 | impl | a | GPA only. Missing at HEAD 39160c0; exported in the uncommitted win32_core.c edit. Gap 7. |
| ntdll!DecodePointer | 344 | 262 | 0.000 | impl | a | As EncodePointer. Gap 7. |
| kernel32!InitializeCriticalSectionAndSpinCount | 20 | 268 | 0.000 | missing | a | GPA only, ours 0. CRT falls back to InitializeCriticalSection (impl). Gap 5. |
| kernel32!FlsAlloc | 1 | 324 | 0.000 | impl | a | GPA only. Missing at HEAD; exported in the uncommitted edit; CRT TLS fallback otherwise. Gap 7. |
| kernel32!InterlockedIncrement | 4001 | 332 | 0.000 | impl | - |  |
| kernel32!GetStdHandle | 3 | 337 | 0.000 | impl | - |  |
| kernel32!GetFileType | 5 | 338 | 0.000 | impl | - |  |
| kernel32!SetHandleCount | 1 | 349 | 0.000 | impl | - |  |
| kernel32!GetCommandLineA | 1 | 350 | 0.000 | impl | - |  |
| kernel32!GetEnvironmentStringsW | 1 | 351 | 0.000 | impl | - |  |
| kernel32!WideCharToMultiByte | 4 | 352 | 0.000 | impl | - |  |
| kernel32!FreeEnvironmentStringsW | 1 | 355 | 0.000 | impl | - |  |
| kernel32!GetLastError | 295835 | 356 | 0.000 | impl | - |  |
| kernel32!GetACP | 1 | 359 | 0.000 | fixed | - | 1252 = Wine. |
| kernel32!IsValidCodePage | 1 | 362 | 0.000 | impl | - |  |
| kernel32!GetCPInfo | 2 | 363 | 0.000 | impl | - |  |
| kernel32!GetStringTypeW | 2 | 366 | 0.000 | impl | - |  |
| kernel32!MultiByteToWideChar | 7 | 367 | 0.000 | impl | - |  |
| kernel32!LCMapStringW | 5 | 371 | 0.000 | impl | - |  |
| kernel32!InterlockedDecrement | 255 | 383 | 0.000 | impl | - |  |
| kernel32!GetModuleFileNameA | 2 | 387 | 0.000 | impl | - |  |
| kernel32!IsProcessorFeaturePresent | 1 | 673 | 0.000 | missing | a | GPA only, arg 0, Wine 0, ours GPA 0; CRT runs its FDIV test. Gap 6. |
| kernel32!SetUnhandledExceptionFilter | 1 | 687 | 0.000 | impl | - |  |
| ntdll!HeapSize | 158 | 690 | 0.000 | impl | - |  |
| ntdll!InitializeCriticalSection | 48 | 694 | 0.000 | impl | - |  |
| ntdll!HeapReAlloc | 5 | 929 | 0.017 | impl | - |  |
| kernel32!CreateSemaphoreA | 88 | 1433 | 0.017 | impl | - |  |
| kernel32!ReleaseSemaphore | 33639 | 1438 | 0.029 | impl | - |  |
| user32!SystemParametersInfoA | 8 | 1763 | 0.032 | partial | a | All 8 actions handled; SET* return differs. Gap 9. |
| kernel32!SetThreadExecutionState | 1 | 1771 | 0.038 | fixed | a? | Ours 0x80000000, Wine 7. Gap 4. |
| combase!CoInitializeEx | 4 | 1772 | 0.038 | impl | - |  |
| ole32!CoInitialize | 5 | 1775 | 0.041 | impl | - |  |
| combase!CoCreateInstance | 6 | 1776 | 0.041 | impl | b | Wine S_OK x6. Ours: XAudio2 registered; GameExplorer and WbemLocator return REGDB_E_CLASSNOTREG. Gaps 1, 2. |
| oleaut32!SysAllocString | 13 | 1779 | 0.046 | impl | - |  |
| oleaut32!SysFreeString | 13 | 1780 | 0.046 | impl | - |  |
| combase!CoUninitialize | 5 | 1781 | 0.046 | impl | - |  |
| kernel32!SetThreadAffinityMask | 1 | 1782 | 0.046 | impl | - |  |
| kernel32!CreateThread | 13 | 1791 | 0.064 | impl | - |  |
| kernel32!SetThreadPriority | 13 | 1792 | 0.064 | fixed | - | Constant matches Wine. |
| kernel32!RaiseException | 13 | 1793 | 0.065 | partial | - | All 13 are 0x406D1388 (thread name), the handled code. |
| kernel32!VirtualQuery | 1 | 1797 | 0.066 | impl | - |  |
| kernel32!CreateEventA | 8 | 1798 | 0.066 | impl | - |  |
| kernel32!ResumeThread | 13 | 1799 | 0.067 | impl | - |  |
| ws2_32!WSAStartup | 1 | 1800 | 0.067 | impl | - |  |
| ws2_32!gethostname | 1 | 1802 | 0.068 | impl | - |  |
| ws2_32!gethostbyname | 1 | 1803 | 0.068 | impl | - |  |
| kernel32!WaitForSingleObject | 35378 | 1804 | 0.068 | impl | - |  |
| ws2_32!inet_ntoa | 1 | 1808 | 0.078 | impl | - |  |
| ws2_32!WSACleanup | 1 | 1810 | 0.078 | fixed | - | Constant matches Wine. |
| d3d9!Direct3DCreate9 | 1 | 1811 | 0.078 | impl | - |  |
| kernel32!GetProcessAffinityMask | 2 | 1893 | 1.518 | impl | - |  |
| kernel32!SetProcessAffinityMask | 4 | 1894 | 1.518 | fixed | - | Constant matches Wine. |
| kernel32!QueryPerformanceFrequency | 2 | 1896 | 1.518 | impl | - |  |
| shell32!SHGetFolderPathA | 196 | 1898 | 1.518 | partial | - | All 196 are CSIDL 0x1A, handled. |
| kernel32!GetFileAttributesA | 2 | 1899 | 1.519 | impl | - |  |
| kernel32!CreateFileA | 400 | 1901 | 1.519 | impl | - |  |
| kernel32!SetFilePointerEx | 952 | 1903 | 1.521 | impl | - |  |
| kernel32!CloseHandle | 390 | 1905 | 1.521 | impl | - |  |
| kernel32!InterlockedCompareExchange | 9205 | 1906 | 1.522 | impl | - |  |
| kernel32!ReadFile | 12922 | 1909 | 1.522 | impl | - |  |
| kernel32!GetFileSizeEx | 1 | 1911 | 1.522 | impl | - |  |
| advapi32!RegOpenKeyExA | 2 | 2035 | 1.536 | impl | - |  |
| kernel32!GetUserDefaultLangID | 2 | 2036 | 1.537 | impl | - |  |
| user32!LoadIconA | 1 | 2037 | 1.537 | fixed | - | Constant matches Wine. |
| user32!LoadCursorA | 1 | 2038 | 1.537 | fixed | - | Constant matches Wine. |
| user32!RegisterClassExA | 1 | 2039 | 1.537 | impl | - |  |
| user32!AdjustWindowRect | 1 | 2040 | 1.538 | impl | - |  |
| user32!CreateWindowExA | 1 | 2041 | 1.538 | impl | - |  |
| ntdll!DefWindowProcA | 25 | 2042 | 1.538 | impl | - |  |
| kernel32!CreateMutexA | 1 | 2047 | 1.554 | impl | - |  |
| kernel32!ReleaseMutex | 1 | 2049 | 1.554 | impl | - |  |
| user32!ShowWindow | 2 | 2050 | 1.554 | impl | - |  |
| user32!UpdateWindow | 2 | 2062 | 1.663 | impl | - |  |
| user32!SendMessageA | 2 | 2066 | 1.663 | impl | - |  |
| user32!EnumDisplayMonitors | 1 | 2070 | 1.665 | impl | - |  |
| d3d9.com!d3d9_CreateDevice | 2 | 5086 | 1.691 | com | ? | +d3d9 channel only; COM methods not measured. |
| kernel32!LoadLibraryA | 1 | 5090 | 1.806 | impl | - | "nvapi.dll" only: Wine 0, ours 0. |
| kernel32!WriteFile | 98 | 5145 | 1.814 | impl | - |  |
| d3d9.com!d3d9_device_Present | 539 | 5153 | 1.816 | com | - |  |
| dinput8!DirectInput8Create | 1 | 5204 | 1.829 | partial | - | dwVersion 0x800, handled. |
| kernel32!CreateDirectoryA | 196 | 5214 | 1.843 | impl | - |  |
| user32!GetWindowThreadProcessId | 540 | 5222 | 1.843 | impl | - |  |
| user32!GetKeyboardLayout | 540 | 5223 | 1.843 | fixed | - | 0x04090409 = Wine. |
| combase!CoSetProxyBlanket | 4 | 5233 | 1.935 | fixed | - | Constant S_OK = Wine. Not reached by us while WbemLocator is unregistered. |
| user32!SetCursor | 1 | 5248 | 1.951 | impl | - |  |
| d3dx9_41!D3DXGetShaderConstantTable | 374 | 5446 | 1.991 | impl | - |  |
| kernel32!WaitForMultipleObjects | 5 | 268415 | 4.383 | impl | - |  |
| d3dx9_41!D3DXMatrixMultiply | 86405 | 268859 | 4.429 | impl | - |  |
| user32!PeekMessageA | 551 | 268865 | 4.429 | impl | - |  |
| user32!TranslateMessage | 13 | 268866 | 4.434 | fixed | a | Ours always 1; Wine 0 once. Gap 8. |
| user32!DispatchMessageA | 13 | 268867 | 4.434 | impl | - |  |
| d3d9.com!d3d9_device_BeginScene | 538 | 268870 | 4.439 | com | - |  |
| xinput1_3!XInputGetState | 4 | 269174 | 4.482 | impl | - | Wine 0x48F; ours the same with no pad. |
| kernel32!Sleep | 772 | 269249 | 4.504 | impl | - |  |
| kernel32!SetEvent | 234 | 269340 | 4.522 | impl | - |  |
| d3dx9_41!D3DXCompileShader | 12 | 275598 | 4.988 | impl | ? | Wine S_OK x12; ours via vkd3d, not measurable here. Gap 3. |
| xinput1_3!XInputGetCapabilities | 4 | 299326 | 6.363 | impl | - | As XInputGetState. |
| ntdll!ExitThread | 4 | 324874 | 7.709 | partial | - | Aborts only off CreateThread threads; callers are NuFileStream CreateThread threads. |

## GetProcAddress calls (module as the game named it):

| module!proc | count | first seq | Wine returns | ours |
|---|---|---|---|---|
| KERNEL32.DLL!FlsAlloc | 1 | 234 | 7beb4570:1 | impl kernel32:imp_FlsAlloc (win32_core.c) |
| KERNEL32.DLL!FlsGetValue | 1 | 235 | 7bebcb50:1 | impl kernel32:imp_FlsGetValue (win32_core.c) |
| KERNEL32.DLL!FlsSetValue | 1 | 236 | 7bebcb64:1 | impl kernel32:imp_FlsSetValue (win32_core.c) |
| KERNEL32.DLL!FlsFree | 1 | 237 | 7beb4590:1 | impl kernel32:imp_FlsFree (win32_core.c) |
| KERNEL32.DLL!EncodePointer | 11 | 240 | 7bf45e50:11 | impl kernel32:imp_EncodePointer (win32_core.c) |
| KERNEL32.DLL!DecodePointer | 23 | 261 | 7bf45910:23 | impl kernel32:imp_DecodePointer (win32_core.c) |
| kernel32.dll!InitializeCriticalSectionAndSpinCount | 1 | 264 | 7beb7650:1 | 0 (not found) |
| KERNEL32!IsProcessorFeaturePresent | 1 | 672 | 7beb7950:1 | 0 (not found) |
