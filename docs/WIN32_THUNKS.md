# Win32 thunk contract (M3)

The same contract as the engine's `docs/THUNK_SLICES.md`, with the differences a
Windows binary brings. Read that file first.

## Calling convention

Mach-O imports are cdecl: the caller pops the arguments, so a thunk only pops
the return address (`g_esp += 4`). Win32 imports are `__stdcall`: the **callee**
pops the arguments.

| Case | Argument read | Stack adjustment on return |
|---|---|---|
| `WINAPI` function with N 32-bit arguments | `MEM32(g_esp + 4 + 4*i)` | `g_esp += 4 + 4*N` |
| 64-bit scalar argument | two slots, low word first | counts as two slots |
| Hidden struct-return pointer | first argument slot | counts as one slot |
| cdecl or varargs (a few CRT-style entries) | same | `g_esp += 4` |
| COM interface method | `this` at `MEM32(g_esp + 4)`, arguments from `+8` | `g_esp += 4 + 4 + 4*N` |

Return values: `g_eax`, plus `g_edx` for a 64-bit scalar; floating-point
returns come back on the x87 stack (`fp_push`), not in a register.

`g_ebx`, `g_esi`, `g_edi` and `g_ebp` must be unchanged on return. `g_ecx` and
`g_edx` are scratch.

Getting the pop count wrong corrupts the guest stack at the call site, so the
argument count of every import is part of the inventory in
`notes/win32_imports.md`, not guesswork at the call site.

## Names

A PE import is named by its symbol alone: `KERNEL32.dll!CreateFileA` becomes
`imp_CreateFileA`. An import by ordinal uses its resolved name; an ordinal that
cannot be resolved becomes `imp_<dll>_ord<N>` and must be logged the first time
it is called.

## COM objects

Only `Direct3DCreate9` is imported by name. Everything else in Direct3D is
reached through COM vtables, so each object the game receives is a block of
guest memory holding a vtable pointer, and that vtable holds guest-callable
addresses registered with `host_func_va`. One object layout per interface, each
derived from the published method order for that interface — never from a
guessed offset.

## Callbacks into guest code

The game hands out `__stdcall` function pointers: the window procedure, the
display-mode and DirectInput enumeration callbacks, and thread start routines.
`guest_call_cdecl` (`engine/runtime/include/guest_call.h`) is safe for them:
it saves `g_esp` before the call and restores it afterwards, so it does not
matter that a stdcall callee pops its own arguments. No new variant is needed.

## Shared code

`runtime/include/win32_thunk.h` is the contract: argument and return macros,
the per-slice export tables, the import resolver, the per-thread last error,
the Win32 handle table and the TIB hook. Win32 handles do not use the engine's
`handles.h`, whose kinds are fixed and whose `CloseHandle`-free design does not
fit: one `CloseHandle` closes any kind, and waits apply across kinds.

## The thread environment block

The sweep found 4,305 segment-prefixed instructions. MSVC code reads `fs:[0]`
for the SEH chain head, `fs:[0x18]` for the TEB itself and `fs:[0x2C]` for the
TLS array.

The lifter already handles the segment: `mov eax, fs:[0]` lifts to
`GLD32(XBOX_FS_BASE)` (for example `sub_004086E0`, the CRT's SEH prolog), and
`XBOX_FS_BASE` is `g_thread_state.ts_fs_base`, which is per thread
(`engine/tools/xrecomp/include/recomp_types.h:284-293`). So nothing changes in
the lifter. What the runtime must do:

- allocate one TIB block in guest memory for every guest thread, including the
  main thread, and set `ts_fs_base` to it before the thread runs guest code;
- fill the fields MSVC reads: `+0x00` SEH chain head (0xFFFFFFFF for an empty
  chain), `+0x04` stack base, `+0x08` stack limit, `+0x18` the TIB's own
  address, `+0x2C` a pointer to the thread's TLS slot array.

The lifter's upstream also detects the MSVC `__SEH_prolog` and `__SEH_epilog`
helpers and rewrites `ebp` from `g_seh_ebp` after them, and it detects
`setjmp`/`longjmp` — all written for MSVC x86 code. The engine currently gates
that detection off, because the two shipping games are Mach-O. Turning it on
for a PE is a small engine change and should happen before any thunk work.

## Switch tables

MSVC compiles a switch to `jmp dword ptr [table + idx*4]`, often behind a byte
index map. The engine's Mach-O sweeper reports none of these, because it only
looks for the `jmp reg` shape of position-independent code. That does not
matter: the lifter's upstream was written for MSVC code and resolves them
itself, as an in-function chain of `goto`s (for example `sub_0040C770` at
0x40C9E3, 14 entries behind a byte map). Of the sweep's 712 indirect jumps,
98 remain unresolved tail jumps in the generated C and need reviewing one by
one.

## Windows paths

The game opens its archives with Windows paths and backslashes, and reads
`HKEY_LOCAL_MACHINE\SOFTWARE\WB Games\LEGOHarryPotter\PathVal` for its install
directory and `LanguageVal` for its language. The file thunks translate paths
to the real install directory; the registry thunks answer from a small fixed
table, not from a real registry.

Saves and settings must never be written back into the Steam install
directory. They go to `~/Library/Application Support/HarryPotterRecomp`, the
same rule the other two games follow.
