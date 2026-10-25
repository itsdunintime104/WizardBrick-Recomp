# Win32 import inventory: lhp_win32.exe

Source: `orig.nosync/lhp_win32.exe` (PE32, MSVC 2005, image base 0x400000, IAT RVA 0xCDE7D0). 14 DLLs, 231 imports, parsed from the import directory (read only, nothing executed).

## Per-bucket counts

| Bucket | Count |
|---|---|
| posix | 53 |
| crt | 52 |
| sdl2 | 29 |
| stub | 17 |
| fs | 27 |
| design | 53 |
| total | 231 |

## Method and caveats

- By-ordinal imports (27 of 231: WS2_32 22, XINPUT1_3 3, OLEAUT32 2) were resolved by reading the export tables of the Wine 11.3 i386 PE builds of `ws2_32.dll`, `oleaut32.dll` and `xinput1_3.dll` (ordinal to name). Wine's tables follow the Microsoft export ordinals, but they were not checked against a Microsoft-shipped DLL. All 27 resolved; none unresolved.
- `args bytes popped` comes from the public SDK signatures (4 bytes per 32-bit argument, 8 for a 64-bit scalar). It was NOT measured against the callers in the binary. Verify against the `ret N` / call-site stack adjustment in lifted code before relying on it.
- Only X3DAudio1_6 functions are marked cdecl (SDK declares them `STDAPIVCALLTYPE`, which is cdecl). This is from the header, not confirmed from the binary. All other rows are stdcall.
- The last column is the IAT slot address (VA, image base 0x400000 applied) for each import, to key thunk tables.
- Bucket `design` here includes all of Winsock, registry, COM/BSTR, XInput, X3DAudio, DirectInput8, D3D9 and D3DX, as instructed. Several are cheap (htonl, htons, ntohs, inet_addr, SysFreeString, D3DXMatrixMultiply).

## Imports

| DLL | Symbol | Ordinal | Args bytes popped | Bucket | Host plan | IAT VA |
|---|---|---|---|---|---|---|
| d3dx9_41.dll | D3DXGetShaderConstantTable | - | 8 | design | D3DX: ID3DXConstantTable from SM bytecode | 0x10dedc8 |
| d3dx9_41.dll | D3DXCreateTextureFromFileInMemoryEx | - | 60 | design | D3DX: IDirect3DTexture9 from DDS/TGA/BMP (15 args) | 0x10dedcc |
| d3dx9_41.dll | D3DXCreateCubeTextureFromFileInMemory | - | 16 | design | D3DX: IDirect3DCubeTexture9 from DDS | 0x10dedd0 |
| d3dx9_41.dll | D3DXCreateEffectFromFileA | - | 32 | design | D3DX: ID3DXEffect from .fx file (8 args) | 0x10dedd4 |
| d3dx9_41.dll | D3DXCreateFontA | - | 48 | design | D3DX: ID3DXFont (12 args); needs glyph rasteriser | 0x10dedd8 |
| d3dx9_41.dll | D3DXCreateRenderToSurface | - | 28 | design | D3DX: ID3DXRenderToSurface (7 args) | 0x10deddc |
| d3dx9_41.dll | D3DXCreateTextureFromFileInMemory | - | 16 | design | D3DX: IDirect3DTexture9 from image bytes | 0x10dede0 |
| d3dx9_41.dll | D3DXSaveSurfaceToFileInMemory | - | 20 | design | D3DX: encode surface (screenshot) into ID3DXBuffer | 0x10dede4 |
| d3dx9_41.dll | D3DXCreateEffect | - | 36 | design | D3DX: ID3DXEffect from memory (9 args) | 0x10dede8 |
| d3dx9_41.dll | D3DXCompileShader | - | 40 | design | D3DX: HLSL compile to SM bytecode (10 args; hardest D3DX item, needs HLSL compiler) | 0x10dedec |
| d3dx9_41.dll | D3DXCreateEffectCompiler | - | 28 | design | D3DX: ID3DXEffectCompiler (7 args) | 0x10dedf0 |
| d3dx9_41.dll | D3DXCreateBuffer | - | 8 | design | D3DX: ID3DXBuffer allocator | 0x10dedf4 |
| d3dx9_41.dll | D3DXMatrixMultiply | - | 12 | design | D3DX: 4x4 matrix multiply (pure math, easy) | 0x10dedf8 |
| d3d9.dll | Direct3DCreate9 | - | 4 | design | Direct3D 9: IDirect3D9 factory (owned by D3D9 lane) | 0x10ded98 |
| DINPUT8.dll | DirectInput8Create | - | 20 | design | DirectInput8: IDirectInput8 vtable over SDL keyboard/mouse/joystick | 0x10de80c |
| XINPUT1_3.dll | XInputGetState | 2 | 8 | design | XInput: GCController poll, or return ERROR_DEVICE_NOT_CONNECTED | 0x10ded30 |
| XINPUT1_3.dll | XInputGetCapabilities | 4 | 12 | design | XInput: fill XINPUT_CAPABILITIES | 0x10ded34 |
| XINPUT1_3.dll | XInputSetState | 3 | 8 | design | XInput: haptics via GCController or no-op | 0x10ded38 |
| WS2_32.dll | WSACleanup | 116 | 0 | design | Winsock: return 0 | 0x10dec68 |
| WS2_32.dll | inet_ntoa | 12 | 4 | design | Winsock: inet_ntoa into guest static buffer (in_addr passed by value) | 0x10dec6c |
| WS2_32.dll | gethostbyname | 52 | 4 | design | Winsock: gethostbyname, build HOSTENT in guest memory | 0x10dec70 |
| WS2_32.dll | gethostname | 57 | 8 | design | Winsock: gethostname | 0x10dec74 |
| WS2_32.dll | closesocket | 3 | 4 | design | Winsock: close() | 0x10dec78 |
| WS2_32.dll | setsockopt | 21 | 20 | design | Winsock: setsockopt (option numbers differ: SOL_SOCKET values) | 0x10dec7c |
| WS2_32.dll | ioctlsocket | 10 | 12 | design | Winsock: FIONBIO -> fcntl, FIONREAD -> ioctl | 0x10dec80 |
| WS2_32.dll | socket | 23 | 12 | design | Winsock: socket() on SOCKET handle space | 0x10dec84 |
| WS2_32.dll | WSAGetLastError | 111 | 0 | design | Winsock: map errno to WSAE* codes | 0x10dec88 |
| WS2_32.dll | bind | 2 | 12 | design | Winsock: bind() | 0x10dec8c |
| WS2_32.dll | htons | 9 | 4 | design | Winsock: htons | 0x10dec90 |
| WS2_32.dll | htonl | 8 | 4 | design | Winsock: htonl | 0x10dec94 |
| WS2_32.dll | listen | 13 | 8 | design | Winsock: listen() | 0x10dec98 |
| WS2_32.dll | accept | 1 | 12 | design | Winsock: accept() | 0x10dec9c |
| WS2_32.dll | connect | 4 | 12 | design | Winsock: connect() | 0x10deca0 |
| WS2_32.dll | inet_addr | 11 | 4 | design | Winsock: inet_addr | 0x10deca4 |
| WS2_32.dll | send | 19 | 16 | design | Winsock: send() | 0x10deca8 |
| WS2_32.dll | sendto | 20 | 24 | design | Winsock: sendto() | 0x10decac |
| WS2_32.dll | recv | 16 | 16 | design | Winsock: recv() | 0x10decb0 |
| WS2_32.dll | ntohs | 15 | 4 | design | Winsock: ntohs | 0x10decb4 |
| WS2_32.dll | recvfrom | 17 | 24 | design | Winsock: recvfrom() | 0x10decb8 |
| WS2_32.dll | WSAStartup | 115 | 8 | design | Winsock: fill WSADATA, return 0 | 0x10decbc |
| X3DAudio1_6.dll | X3DAudioCalculate | - | cdecl, 20 (caller pops) | design | X3DAudio: 3D positional math (emitter/listener -> DSP settings) | 0x10decfc |
| X3DAudio1_6.dll | X3DAudioInitialize | - | cdecl, 12 (caller pops) | design | X3DAudio: store speaker mask + speed of sound | 0x10ded00 |
| KERNEL32.dll | OutputDebugStringW | - | 4 | posix | convert and write to stderr | 0x10de874 |
| KERNEL32.dll | SetEnvironmentVariableA | - | 8 | crt | guest-side env table (not host setenv) | 0x10de878 |
| KERNEL32.dll | CompareStringW | - | 24 | crt | locale-less wide compare, CSTR_* result | 0x10de87c |
| KERNEL32.dll | GetEnvironmentStrings | - | 0 | crt | build double-NUL block in guest memory | 0x10de880 |
| KERNEL32.dll | GetDriveTypeA | - | 4 | fs | return DRIVE_FIXED | 0x10de884 |
| KERNEL32.dll | FreeEnvironmentStringsA | - | 4 | crt | free guest block | 0x10de888 |
| KERNEL32.dll | CompareStringA | - | 24 | crt | byte compare with flags, CSTR_* result | 0x10de88c |
| KERNEL32.dll | SetEndOfFile | - | 4 | fs | ftruncate at current offset | 0x10de890 |
| KERNEL32.dll | GetTimeZoneInformation | - | 4 | crt | fill TIME_ZONE_INFORMATION from tm_gmtoff | 0x10de894 |
| KERNEL32.dll | WriteConsoleW | - | 20 | posix | convert UTF-16 to UTF-8, write(1/2) | 0x10de898 |
| KERNEL32.dll | GetConsoleOutputCP | - | 0 | stub | return 437 | 0x10de89c |
| KERNEL32.dll | WriteConsoleA | - | 20 | posix | write(1/2) to host stdio | 0x10de8a0 |
| KERNEL32.dll | GetLocaleInfoW | - | 16 | crt | return fixed en-US strings per LCTYPE | 0x10de8a4 |
| KERNEL32.dll | IsValidLocale | - | 8 | crt | TRUE for en-US | 0x10de8a8 |
| KERNEL32.dll | EnumSystemLocalesA | - | 8 | crt | call callback once with en-US id | 0x10de8ac |
| KERNEL32.dll | GetUserDefaultLCID | - | 0 | crt | return 0x0409 | 0x10de8b0 |
| KERNEL32.dll | GetStringTypeW | - | 16 | crt | ctype-class table (C1/C2/C3 bits) | 0x10de8b4 |
| KERNEL32.dll | GetStringTypeA | - | 20 | crt | ctype-class table for single-byte CP | 0x10de8b8 |
| KERNEL32.dll | GetLocaleInfoA | - | 16 | crt | return fixed en-US strings per LCTYPE | 0x10de8bc |
| KERNEL32.dll | SetStdHandle | - | 8 | crt | store in guest std-handle slots | 0x10de8c0 |
| KERNEL32.dll | IsValidCodePage | - | 4 | crt | TRUE for 1252/437/65001 | 0x10de8c4 |
| KERNEL32.dll | GetOEMCP | - | 0 | crt | return 437 | 0x10de8c8 |
| KERNEL32.dll | GetACP | - | 0 | crt | return 1252 | 0x10de8cc |
| KERNEL32.dll | FlushFileBuffers | - | 4 | fs | fsync | 0x10de8d0 |
| KERNEL32.dll | GetConsoleMode | - | 8 | stub | return FALSE (not a console) | 0x10de8d4 |
| KERNEL32.dll | GetConsoleCP | - | 0 | stub | return 437 | 0x10de8d8 |
| KERNEL32.dll | VirtualQuery | - | 12 | posix | mmap-backed region table lookup in guest arena | 0x10de8dc |
| KERNEL32.dll | VirtualAlloc | - | 16 | posix | carve pages from guest arena; mprotect for protection flags | 0x10de8e0 |
| KERNEL32.dll | FatalAppExitA | - | 8 | posix | print message, abort() | 0x10de8e4 |
| KERNEL32.dll | GetSystemTimeAsFileTime | - | 4 | posix | gettimeofday, convert to 100ns since 1601 | 0x10de8e8 |
| KERNEL32.dll | GetCurrentProcessId | - | 0 | posix | getpid | 0x10de8ec |
| KERNEL32.dll | GetTickCount | - | 0 | posix | mach_absolute_time in ms (32-bit wrap) | 0x10de8f0 |
| KERNEL32.dll | VirtualFree | - | 12 | posix | release or decommit in guest arena; madvise | 0x10de8f4 |
| KERNEL32.dll | HeapCreate | - | 12 | crt | new arena object in guest heap table | 0x10de8f8 |
| KERNEL32.dll | HeapDestroy | - | 4 | crt | free arena object | 0x10de8fc |
| KERNEL32.dll | GetEnvironmentStringsW | - | 0 | crt | build wide double-NUL block | 0x10de900 |
| KERNEL32.dll | Sleep | - | 4 | posix | nanosleep (0 = sched_yield) | 0x10de904 |
| KERNEL32.dll | DeleteFileA | - | 4 | fs | translate path, unlink | 0x10de908 |
| KERNEL32.dll | ReadFile | - | 20 | fs | read(); sync only (OVERLAPPED unsupported) | 0x10de90c |
| KERNEL32.dll | GetFileSize | - | 8 | fs | fstat st_size split hi/lo | 0x10de910 |
| KERNEL32.dll | CloseHandle | - | 4 | fs | handle-table close (fd, event, thread, mutex...) | 0x10de914 |
| KERNEL32.dll | CreateFileA | - | 28 | fs | translate path, open() with disposition/access mapping | 0x10de918 |
| KERNEL32.dll | GetTempFileNameA | - | 16 | fs | mkstemp in temp dir, return Windows-style name | 0x10de91c |
| KERNEL32.dll | CreateEventA | - | 16 | posix | pthread cond+mutex object in handle table | 0x10de920 |
| KERNEL32.dll | WaitForSingleObject | - | 8 | posix | handle-table wait (cond/mutex/semaphore/thread join, timeout) | 0x10de924 |
| KERNEL32.dll | SetEvent | - | 4 | posix | signal cond in handle table | 0x10de928 |
| KERNEL32.dll | GetExitCodeThread | - | 8 | posix | read stored thread result | 0x10de92c |
| KERNEL32.dll | WaitForMultipleObjectsEx | - | 20 | posix | handle-table multi-wait (poll or cond) | 0x10de930 |
| KERNEL32.dll | CreateThread | - | 24 | posix | pthread_create running guest entry on own guest stack; handle-table entry | 0x10de934 |
| KERNEL32.dll | GetCurrentThreadId | - | 0 | posix | pthread_threadid_np | 0x10de938 |
| KERNEL32.dll | DebugBreak | - | 0 | stub | no-op | 0x10de93c |
| KERNEL32.dll | ReleaseSemaphore | - | 12 | posix | dispatch_semaphore or cond count in handle table | 0x10de940 |
| KERNEL32.dll | InitializeCriticalSection | - | 4 | posix | pthread recursive mutex stored beside CRITICAL_SECTION | 0x10de944 |
| KERNEL32.dll | SetFilePointerEx | - | 20 | fs | lseek, 64-bit | 0x10de948 |
| KERNEL32.dll | GetFileSizeEx | - | 8 | fs | fstat st_size | 0x10de94c |
| KERNEL32.dll | MoveFileA | - | 8 | fs | translate both paths, rename | 0x10de950 |
| KERNEL32.dll | GetLastError | - | 0 | crt | per-thread guest errno slot | 0x10de954 |
| KERNEL32.dll | FindNextFileA | - | 8 | fs | readdir on handle-table DIR*, wildcard match | 0x10de958 |
| KERNEL32.dll | FileTimeToSystemTime | - | 8 | posix | arithmetic conversion, gmtime_r | 0x10de95c |
| KERNEL32.dll | FileTimeToLocalFileTime | - | 8 | posix | add localtime_r gmtoff | 0x10de960 |
| KERNEL32.dll | CreateDirectoryA | - | 8 | fs | translate path, mkdir | 0x10de964 |
| KERNEL32.dll | GetExitCodeProcess | - | 8 | stub | return FALSE | 0x10de968 |
| KERNEL32.dll | CreateProcessA | - | 40 | stub | return FALSE, set last error (launcher/crash-reporter path) | 0x10de96c |
| KERNEL32.dll | GetStdHandle | - | 4 | crt | return fixed pseudo-handles for stdin/out/err | 0x10de970 |
| KERNEL32.dll | RaiseException | - | 16 | crt | SEH dispatch emulation; abort if unhandled | 0x10de974 |
| KERNEL32.dll | CreateSemaphoreA | - | 16 | posix | semaphore object in handle table | 0x10de978 |
| KERNEL32.dll | InterlockedIncrement | - | 4 | posix | __atomic_add_fetch | 0x10de97c |
| KERNEL32.dll | InterlockedDecrement | - | 4 | posix | __atomic_sub_fetch | 0x10de980 |
| KERNEL32.dll | InterlockedCompareExchange | - | 12 | posix | __atomic_compare_exchange | 0x10de984 |
| KERNEL32.dll | QueryPerformanceCounter | - | 4 | posix | mach_absolute_time scaled to fixed frequency | 0x10de988 |
| KERNEL32.dll | QueryPerformanceFrequency | - | 4 | posix | return fixed 1e9 (or timebase-scaled) | 0x10de98c |
| KERNEL32.dll | SetProcessAffinityMask | - | 8 | posix | return TRUE, ignore | 0x10de990 |
| KERNEL32.dll | GetProcessAffinityMask | - | 12 | posix | report 1..N cores from sysctl | 0x10de994 |
| KERNEL32.dll | GetCurrentProcess | - | 0 | posix | return pseudo-handle -1 | 0x10de998 |
| KERNEL32.dll | GetProcAddress | - | 8 | crt | lookup in thunk table by name; NULL if absent (hard: must return guest-callable thunk addr) | 0x10de99c |
| KERNEL32.dll | LoadLibraryA | - | 4 | crt | return fake module handle for known DLL names, else NULL | 0x10de9a0 |
| KERNEL32.dll | FreeLibrary | - | 4 | crt | no-op TRUE | 0x10de9a4 |
| KERNEL32.dll | VerifyVersionInfoA | - | 16 | stub | return TRUE | 0x10de9a8 |
| KERNEL32.dll | VerSetConditionMask | - | 16 | stub | return 0 (64-bit result in EDX:EAX) | 0x10de9ac |
| KERNEL32.dll | ReleaseMutex | - | 4 | posix | pthread_mutex_unlock on handle-table mutex | 0x10de9b0 |
| KERNEL32.dll | CreateMutexA | - | 12 | posix | pthread recursive mutex in handle table | 0x10de9b4 |
| KERNEL32.dll | GetFileAttributesA | - | 4 | fs | translate path, stat to attributes | 0x10de9b8 |
| KERNEL32.dll | ExpandEnvironmentStringsA | - | 12 | fs | expand %VAR% from guest env table | 0x10de9bc |
| KERNEL32.dll | WriteFile | - | 20 | fs | write() | 0x10de9c0 |
| KERNEL32.dll | GetFileInformationByHandle | - | 8 | fs | fstat into BY_HANDLE_FILE_INFORMATION | 0x10de9c4 |
| KERNEL32.dll | CreatePipe | - | 16 | stub | return FALSE | 0x10de9c8 |
| KERNEL32.dll | DeleteCriticalSection | - | 4 | posix | pthread_mutex_destroy | 0x10de9cc |
| KERNEL32.dll | TryEnterCriticalSection | - | 4 | posix | pthread_mutex_trylock | 0x10de9d0 |
| KERNEL32.dll | EnterCriticalSection | - | 4 | posix | pthread_mutex_lock | 0x10de9d4 |
| KERNEL32.dll | LeaveCriticalSection | - | 4 | posix | pthread_mutex_unlock | 0x10de9d8 |
| KERNEL32.dll | ExitThread | - | 4 | posix | record code, pthread_exit | 0x10de9dc |
| KERNEL32.dll | TlsSetValue | - | 8 | crt | per-thread slot write | 0x10de9e0 |
| KERNEL32.dll | ResumeThread | - | 4 | posix | start suspended thread (CREATE_SUSPENDED needs deferred pthread_create) | 0x10de9e4 |
| KERNEL32.dll | TlsGetValue | - | 4 | crt | per-thread slot read | 0x10de9e8 |
| KERNEL32.dll | MultiByteToWideChar | - | 24 | crt | CP1252/UTF-8 decode to UTF-16 | 0x10de9ec |
| KERNEL32.dll | GetTimeFormatA | - | 24 | posix | strftime-style formatting of time | 0x10de9f0 |
| KERNEL32.dll | GetDateFormatA | - | 24 | posix | strftime-style formatting of date | 0x10de9f4 |
| KERNEL32.dll | GetModuleFileNameA | - | 12 | crt | return fake Windows path of the exe | 0x10de9f8 |
| KERNEL32.dll | GetUserGeoID | - | 4 | crt | return fixed GEOID (US 244) | 0x10de9fc |
| KERNEL32.dll | GetUserDefaultLangID | - | 0 | crt | return 0x0409 | 0x10dea00 |
| KERNEL32.dll | GetModuleHandleA | - | 4 | crt | NULL = image base 0x400000; known DLLs = fake handle | 0x10dea04 |
| KERNEL32.dll | TlsAlloc | - | 0 | crt | slot index from guest TLS table | 0x10dea08 |
| KERNEL32.dll | SetThreadPriority | - | 8 | posix | pthread_setschedparam or ignore | 0x10dea0c |
| KERNEL32.dll | SetFileTime | - | 16 | fs | futimes | 0x10dea10 |
| KERNEL32.dll | SystemTimeToFileTime | - | 8 | posix | timegm arithmetic | 0x10dea14 |
| KERNEL32.dll | GetSystemTime | - | 4 | posix | gettimeofday, gmtime_r into SYSTEMTIME | 0x10dea18 |
| KERNEL32.dll | FindClose | - | 4 | fs | closedir, free handle | 0x10dea1c |
| KERNEL32.dll | FindFirstFileA | - | 8 | fs | translate path, opendir + wildcard match | 0x10dea20 |
| KERNEL32.dll | CompareFileTime | - | 8 | posix | 64-bit compare | 0x10dea24 |
| KERNEL32.dll | GetVolumeInformationA | - | 32 | fs | fixed volume label/serial/NTFS | 0x10dea28 |
| KERNEL32.dll | SetFilePointer | - | 16 | fs | lseek 32-bit with high word pointer | 0x10dea2c |
| KERNEL32.dll | GetLogicalDrives | - | 0 | fs | return mask with C: only | 0x10dea30 |
| KERNEL32.dll | WaitForMultipleObjects | - | 16 | posix | handle-table multi-wait (poll or cond) | 0x10dea34 |
| KERNEL32.dll | SetThreadExecutionState | - | 4 | stub | return ES_CONTINUOUS | 0x10dea38 |
| KERNEL32.dll | OutputDebugStringA | - | 4 | posix | write to stderr | 0x10dea3c |
| KERNEL32.dll | SetThreadAffinityMask | - | 8 | posix | return previous mask, ignore (macOS has no hard affinity) | 0x10dea40 |
| KERNEL32.dll | GetCurrentThread | - | 0 | posix | return pseudo-handle -2 | 0x10dea44 |
| KERNEL32.dll | GetVersionExA | - | 4 | posix | fill OSVERSIONINFO with fixed Windows 7/10 values | 0x10dea48 |
| KERNEL32.dll | InterlockedExchange | - | 8 | posix | __atomic_exchange | 0x10dea4c |
| KERNEL32.dll | RtlUnwind | - | 16 | crt | SEH unwind emulation (hard: must walk guest exception chain) | 0x10dea50 |
| KERNEL32.dll | GetCommandLineA | - | 0 | crt | guest-memory command line string | 0x10dea54 |
| KERNEL32.dll | HeapFree | - | 12 | crt | free from arena | 0x10dea58 |
| KERNEL32.dll | HeapAlloc | - | 12 | crt | malloc in arena (zero if HEAP_ZERO_MEMORY) | 0x10dea5c |
| KERNEL32.dll | GetProcessHeap | - | 0 | crt | return default arena handle | 0x10dea60 |
| KERNEL32.dll | GetStartupInfoA | - | 4 | crt | zero STARTUPINFO, set cb | 0x10dea64 |
| KERNEL32.dll | TerminateProcess | - | 8 | posix | _exit(code) | 0x10dea68 |
| KERNEL32.dll | UnhandledExceptionFilter | - | 4 | crt | print fault, return EXCEPTION_EXECUTE_HANDLER | 0x10dea6c |
| KERNEL32.dll | SetUnhandledExceptionFilter | - | 4 | crt | store filter, return previous | 0x10dea70 |
| KERNEL32.dll | IsDebuggerPresent | - | 0 | stub | return 0 | 0x10dea74 |
| KERNEL32.dll | ExitProcess | - | 4 | posix | exit(code) | 0x10dea78 |
| KERNEL32.dll | SetConsoleCtrlHandler | - | 8 | stub | return TRUE | 0x10dea7c |
| KERNEL32.dll | HeapReAlloc | - | 16 | crt | realloc in arena | 0x10dea80 |
| KERNEL32.dll | LCMapStringA | - | 24 | crt | toupper/tolower/sortkey subset | 0x10dea84 |
| KERNEL32.dll | WideCharToMultiByte | - | 32 | crt | UTF-16 to CP1252/UTF-8 encode | 0x10dea88 |
| KERNEL32.dll | LCMapStringW | - | 24 | crt | towupper/towlower subset | 0x10dea8c |
| KERNEL32.dll | GetCPInfo | - | 8 | crt | fill CPINFO for 1252/437 | 0x10dea90 |
| KERNEL32.dll | TlsFree | - | 4 | crt | release slot | 0x10dea94 |
| KERNEL32.dll | SetLastError | - | 4 | crt | write per-thread guest errno slot | 0x10dea98 |
| KERNEL32.dll | HeapSize | - | 12 | crt | malloc_size of block | 0x10dea9c |
| KERNEL32.dll | SetHandleCount | - | 4 | crt | return arg | 0x10deaa0 |
| KERNEL32.dll | GetFileType | - | 4 | fs | fstat: DISK/CHAR/PIPE | 0x10deaa4 |
| KERNEL32.dll | FreeEnvironmentStringsW | - | 4 | crt | free guest block | 0x10deaa8 |
| USER32.dll | MessageBoxA | - | 16 | sdl2 | SDL_ShowSimpleMessageBox | 0x10debb4 |
| USER32.dll | AdjustWindowRect | - | 12 | sdl2 | identity (client rect = window rect) | 0x10debb8 |
| USER32.dll | CreateWindowExA | - | 48 | sdl2 | SDL_CreateWindow, map HWND to SDL_Window in table (WM_CREATE sent synchronously) | 0x10debbc |
| USER32.dll | EnumDisplayMonitors | - | 16 | sdl2 | SDL_GetNumVideoDisplays, invoke callback per display | 0x10debc0 |
| USER32.dll | PostQuitMessage | - | 4 | sdl2 | queue WM_QUIT | 0x10debc4 |
| USER32.dll | DefWindowProcA | - | 16 | sdl2 | default handling table (WM_CLOSE -> DestroyWindow, others 0) | 0x10debc8 |
| USER32.dll | MessageBoxW | - | 16 | sdl2 | convert, SDL_ShowSimpleMessageBox | 0x10debcc |
| USER32.dll | PeekMessageA | - | 20 | sdl2 | SDL_PollEvent translated to MSG queue (WM_KEYDOWN, WM_MOUSEMOVE, WM_ACTIVATE...) | 0x10debd0 |
| USER32.dll | TranslateMessage | - | 4 | sdl2 | synthesize WM_CHAR from SDL text input | 0x10debd4 |
| USER32.dll | DispatchMessageA | - | 4 | sdl2 | call guest WndProc (guest callback) | 0x10debd8 |
| USER32.dll | RegisterClassExA | - | 4 | sdl2 | store WNDCLASSEX in guest class table, return atom | 0x10debdc |
| USER32.dll | GetSystemMetrics | - | 4 | sdl2 | SDL display size / fixed metrics | 0x10debe0 |
| USER32.dll | SetForegroundWindow | - | 4 | sdl2 | SDL_RaiseWindow | 0x10debe4 |
| USER32.dll | IsIconic | - | 4 | sdl2 | SDL_GetWindowFlags minimized | 0x10debe8 |
| USER32.dll | ShowWindow | - | 8 | sdl2 | SDL_ShowWindow/HideWindow/Minimize | 0x10debec |
| USER32.dll | UpdateWindow | - | 4 | sdl2 | send pending WM_PAINT or no-op | 0x10debf0 |
| USER32.dll | SetCursor | - | 4 | sdl2 | SDL_SetCursor / SDL_ShowCursor | 0x10debf4 |
| USER32.dll | SystemParametersInfoA | - | 16 | sdl2 | return fixed values for known actions | 0x10debf8 |
| USER32.dll | GetWindowThreadProcessId | - | 8 | sdl2 | return main thread id | 0x10debfc |
| USER32.dll | GetKeyboardLayout | - | 4 | sdl2 | return 0x04090409 | 0x10dec00 |
| USER32.dll | SendMessageA | - | 16 | sdl2 | call guest WndProc synchronously | 0x10dec04 |
| USER32.dll | MoveWindow | - | 24 | sdl2 | SDL_SetWindowPosition/Size | 0x10dec08 |
| USER32.dll | LoadCursorA | - | 8 | sdl2 | return fake cursor handle | 0x10dec0c |
| USER32.dll | LoadIconA | - | 8 | sdl2 | return fake icon handle | 0x10dec10 |
| USER32.dll | DestroyWindow | - | 4 | sdl2 | SDL_DestroyWindow, send WM_DESTROY | 0x10dec14 |
| USER32.dll | SetCursorPos | - | 8 | sdl2 | SDL_WarpMouseGlobal | 0x10dec18 |
| USER32.dll | GetForegroundWindow | - | 0 | sdl2 | return window with SDL input focus | 0x10dec1c |
| USER32.dll | GetWindowLongA | - | 8 | sdl2 | read guest window-data table (GWL_STYLE, GWL_USERDATA...) | 0x10dec20 |
| USER32.dll | SetWindowPos | - | 28 | sdl2 | SDL_SetWindowPosition/Size/BordersFlag | 0x10dec24 |
| GDI32.dll | DeleteDC | - | 4 | stub | return TRUE | 0x10de83c |
| GDI32.dll | ExtEscape | - | 24 | stub | return 0 (vendor/driver escape) | 0x10de840 |
| GDI32.dll | CreateDCA | - | 16 | stub | return NULL | 0x10de844 |
| comdlg32.dll | GetOpenFileNameA | - | 4 | stub | return FALSE (dev dialog) | 0x10ded68 |
| ADVAPI32.dll | RegOpenKeyExA | - | 20 | design | registry: key table, HKLM/HKCU roots in host config file | 0x10de7d0 |
| ADVAPI32.dll | RegCloseKey | - | 4 | design | registry: close key handle | 0x10de7d4 |
| ADVAPI32.dll | RegSetValueExA | - | 24 | design | registry: write value to host store | 0x10de7d8 |
| ADVAPI32.dll | RegQueryValueExA | - | 24 | design | registry: read value from host store | 0x10de7dc |
| SHELL32.dll | ShellExecuteA | - | 24 | stub | return 33 (success >32), open nothing | 0x10deb7c |
| SHELL32.dll | SHCreateDirectoryExA | - | 12 | fs | translate path, mkdir -p | 0x10deb80 |
| SHELL32.dll | SHGetFolderPathA | - | 20 | fs | return path under host app-support dir, Windows-style | 0x10deb84 |
| ole32.dll | CoInitializeEx | - | 8 | design | COM: return S_OK, track apartment | 0x10dee30 |
| ole32.dll | CoUninitialize | - | 0 | design | COM: no-op | 0x10dee34 |
| ole32.dll | CoCreateInstance | - | 20 | design | COM: factory table by CLSID, else REGDB_E_CLASSNOTREG (hard: callers use vtables) | 0x10dee38 |
| ole32.dll | CoInitialize | - | 4 | design | COM: return S_OK | 0x10dee3c |
| ole32.dll | CoSetProxyBlanket | - | 32 | design | COM: return S_OK (WMI/hardware query) | 0x10dee40 |
| OLEAUT32.dll | SysAllocString | 2 | 4 | design | BSTR: length-prefixed UTF-16 alloc in guest heap | 0x10deb48 |
| OLEAUT32.dll | SysFreeString | 6 | 4 | design | BSTR: free | 0x10deb4c |
