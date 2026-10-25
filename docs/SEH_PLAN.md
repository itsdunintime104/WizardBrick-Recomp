# SEH and C++ exception plan

Decision: **option (a)**. A real exception is fatal, with a log that names it.
No normal-play path raises one. The work that matters is on the normal path:
the SEH/EH frame helpers must hand `ebp` back to their callers (slice 1), and
the SEH4 function pieces must be lifted (slice 2, owned by the lift-review fix).

Evidence scripts and listings: `<job tmp>/seh/`
(`eh_scan.py`, `counts.py`, `cxx_try.txt`, `seh_scopes.txt`, `dis.sh`). Run them
with `~/miniforge3/bin/python3` (it has `pefile`). "Library"
below means CRT (0xA67000 and up) or Dinkumware STL template code; "game" means
code below 0x948000 (the game's own subsystems, for example 0x929E30 and
0x93CB90). The STL template code between 0x948000 and 0xA67000 is mixed with
game code; the functions cited below were identified one by one.

## 1. C++ exceptions

| Item | Count | Evidence |
|---|---|---|
| FuncInfo records (magic 0x19930522) | 1,451 | scan of .rdata; each has a `mov eax, FuncInfo; jmp ___CxxFrameHandler3` thunk; all 1,451 jump to 0xA67BFB |
| EHFlags | 1 in all 1,451 | /EHs: `catch(...)` does not catch SEH exceptions (access violations) |
| FuncInfo with try blocks | 6 (5 below 0xA67000, 1 CRT) | `cxx_try.txt` |
| Catch clauses | 9, all `catch(...)` | no typed catch anywhere |
| Frame forms | 1,156 inline (`push -1; push thunk; mov eax, fs:[0]`); 24 via `__EH_prolog3*` | `counts.py` |
| `_CxxThrowException` (0xA6CC7B) call sites | 26 in 23 functions; 0 below 0x948000 | `eh_scan.py` |

The 6 try functions. All are STL exception-safety code:

| Function | Catch handler(s) | What it does |
|---|---|---|
| 0x999690 | 0x999703, 0x999795 | `basic_string::_Copy` shape: allocate (0x948AB0); `catch(...)` retries with the exact size; inner `catch(...)` frees and rethrows (0x9997C0) |
| 0x9D45B0 | 0x9D471F, 0x9D472B | same two-handler shape, rethrow at 0x9D472F (identification by shape, not confirmed) |
| 0x9D4800 | 0x9D496F, 0x9D497B | same, rethrow at 0x9D497F (by shape, not confirmed) |
| 0x9F7920 | 0x9F7968 | `ostream::_Osfx`: `if (flags & unitbuf) flush(); catch(...) {}` |
| 0xA065B0 | 0xA0679F | stream operator: `catch(...) { setstate(badbit, true); }`; rethrows only if `exceptions()` has the bit (test at 0xA067BB) |
| (CRT, owner not found) | 0xA6F5C5 | CRT EH internals; rethrow at 0xA6F5D7 |

The 26 throw sites (types from the ThrowInfo pushed before the call):
`bad_alloc` 6 (0x948930..0x948AB0 allocators, CRT `operator new` 0xA67B28),
`length_error` 5, `out_of_range` 1, `invalid_argument` 1, `runtime_error` 1
(`_Locinfo`), `bad_cast` 2 (`use_facet`), `ios_base::failure` 4 (0x9CA380, only
when a stream's exception mask is set), `bad_exception` 1 (CRT `FindHandler`),
and 5 rethrows inside the catch funclets above. Every one is an error path:
allocation failure, size overflow, a missing locale facet, or a stream error
with exceptions enabled.

Boot and normal play: none of the 6 try functions is in `docs/STARTUP_REQS.md`.
String copies (0x999690) and stream output (0x9F7920) are likely on normal
paths, but their catch clauses run only after a throw, and no throw site is
reached unless an allocation or a size check fails. **Not confirmed by a run:**
no trace of throw sites exists yet (slice 5 records it).

## 2. `__try`/`__except` and `__try`/`__finally`

| Where | Frames | Evidence |
|---|---|---|
| Game code | 2, both SEH3 (`__except_handler3` 0xA6B588) | `seh_scopes.txt` |
| CRT | 94 `__SEH_prolog4` callers, 2 inline SEH4, 1 more SEH4 | `__except_handler4` 0xA74320 |
| `IsBadReadPtr`/`IsBadWritePtr`/`IsBadCodePtr`, `AddVectoredExceptionHandler`, `MiniDumpWriteDump` | not imported | IAT scan |

The two game frames:

- **0x929E30 `SetThreadName`**, scope table 0xDFBCF8, one record: filter
  0x929E85, handler 0x929E9D. The filter returns CONTINUE_EXECUTION for
  0x406D1388. `runtime/src/win32_core.c:344` already returns normally for that
  code; this is equivalent.
- **0x93CB90 `DetectCpuFeatures`**, scope table 0xDFBD08, one record: filter
  0x93CEB6 (`mov eax,1; ret` = EXECUTE_HANDLER), handler 0x93CEBC (sets
  [0x1042E7C] = 0, "OS has no SSE"). The `__try` body is one `orps xmm0,xmm0`
  at 0x93CEB1, a probe that faults only if the OS does not enable SSE. The lift
  emits `xmm0 = XMM_OR(xmm0, xmm0)` (`generated.nosync/lhp_0074.c:26109`), which
  cannot fault, so the handler never runs. The handler block is not lifted (no
  `loc_0093CEBC`), which is correct.

CRT frames are `__try/__finally` around lock pairs (`_free`, `_realloc`,
`_fread_s`, `_fclose`, `__ioinit`, ...) plus `__tmainCRTStartup`'s
`__except(_XcptFilter(...))`. The `__finally` blocks run on the normal path as
local calls: `mov [ebp-4],-2; call <finally>` followed by
`call __SEH_epilog4; ret`. These are the C3 targets in `notes/lift_review.md`.
`__local_unwind4` (0xA7D1B8) has 4 callers, all in EH/longjmp runtime
(0xA7D248, 0xA6B6A1, 0xA7D2F4, 0xA7D28E); no compiled function calls it on a
normal `return`, so normal exits need no unwinder.

## 3. Destructor-only frames

1,445 of 1,451 FuncInfo records have no try block (1,406 below 0xA67000). At
run time they do only two things on the normal path: link/unlink `fs:[0]`
and write the state at `[ebp-4]`. Both are plain memory operations the lifter
already emits. The handler thunks run only during dispatch. They need nothing.

## 4. Start-up and crash handlers

- The game installs **no** unhandled-exception filter. All 5
  `SetUnhandledExceptionFilter` call sites are CRT or import thunks: 0xA76A05
  (`_initterm_e` entry; installs `__CxxUnhandledExceptionFilter` 0xA769C8,
  which turns an escaped 0xE06D7363 into `terminate()`), `__invoke_watson`
  0xA6CB0A, `_abort` 0xA72C52 (code 0x40000015, then `_exit(3)`),
  `__report_gsfailure` 0xA80269, thunk 0xAC26DA.
- `__invoke_watson` and `__report_gsfailure` call `IsDebuggerPresent`,
  `SetUnhandledExceptionFilter(NULL)`, `UnhandledExceptionFilter(&ptrs)`, then
  `TerminateProcess`. They need: filter store returns the previous value;
  `UnhandledExceptionFilter` logs and returns EXCEPTION_EXECUTE_HANDLER (1).
- `__tmainCRTStartup` (0xA691B3) wraps WinMain in `__try/__except(_XcptFilter)`.
  It needs a working `__SEH_prolog4` frame (slice 1). It needs no dispatch
  unless WinMain raises.
- `RaiseException` callers: 0x929E7D (`SetThreadName`), 0xA6CCBB
  (`_CxxThrowException`), 0xA78E5F (`__raise_exc_ex`, unmasked FP exceptions
  only), thunk 0xAC2560. `RtlUnwind` caller: thunk 0xAC26AA only (used by
  `__global_unwind2` and `_EH4_GlobalUnwind`, dispatch only).
- Today any code other than 0x406D1388 prints `UNSUPPORTED
  KERNEL32.dll!RaiseException ... (code, guest return)` and aborts
  (`runtime/src/win32_core.c:360-364`); `RtlUnwind` does the same
  (`win32_core.c:368-376`).

## 5. What the engine does for the Mach-O games

- C++ exceptions are not supported: `imp___Unwind_Resume` and
  `imp____gxx_personality_v0` call `rt_die`
  (`MarvelRecomp/engine/runtime/src/thunk_rt.c:238-245`). That is option (a).
- Reusable mechanism: guest `setjmp`/`longjmp` paired with a native `setjmp`
  taken at the guest call site (`engine/tools/xrecomp/include/recomp_types.h:297-336`,
  `lifter.py:2636-2667`). It unwinds the native C stack for real. This is the
  base option (b) or (c) would build on. HP itself has no MSVC `setjmp`/`longjmp`
  (the "VC20" jmp_buf cookie occurs 0 times).
- `guest_mem.c:52-69` uses `sigsetjmp`/`siglongjmp` for a memory probe only.
- The lifter detects `__SEH_prolog`/`__SEH_epilog` and bridges `ebp` through
  `g_seh_ebp` (`lifter.py:1180-1222`, `2700-2712`, `2749-2756`). Its byte markers
  (`64 A1 00000000` + `lea ebp,[esp+N]`; epilog `C9 51 C3`) match the old
  MSVC helpers, not HP's `__SEH_prolog4` (`push fs:[0]`), `__EH_prolog3`
  (`mov ebp,eax`) or their epilogs (`mov esp,ebp; pop ebp; push ecx; ret`).
  See slice 1.

## Confirmed normal-path bug (not an exception)

The HP lift does not bridge `ebp` through any of the 8 frame helpers. Example,
`__tmainCRTStartup` at the boot entry:
`generated.nosync/lhp_0087.c:25867`
`PUSH32(esp, 0x00A691BFu); RECOMP_ABI_CALL(0x00A74290u, sub_00A74290);` with no
`ebp = g_seh_ebp` after it, and `sub_00A6EE6B` (`lhp_0088.c:10577-10609`) sets
its local `ebp = eax` and returns without publishing it. The caller then
addresses locals through a stale `ebp` (for example `ecx = ebp + -40` at
`lhp_0087.c:10322`). `tools/xrecomp/run.py:61` builds `BatchTranslator` without
`seh_prolog`/`seh_epilog`, and detection finds none of HP's helpers.

| Helper | VA | Call sites (jmp) |
|---|---|---|
| `__SEH_prolog4` | 0xA74290 | 94 |
| `__SEH_epilog4` | 0xA742D5 | 93 |
| `__EH_prolog3` | 0xA6EE6B | 18 |
| `__EH_prolog3_catch` | 0xA6EE9E | 1 |
| `__EH_prolog3_GS` | 0xA6EED4 | 5 |
| `__EH_prolog3_catch_GS` | 0xA6EF0A | 0 |
| `__EH_epilog3` | 0xA6EF43 | 15 (2) |
| `__EH_epilog3_GS` | 0xA6EF57 | 5 |

## Options

| Option | What it needs | Verdict |
|---|---|---|
| (a) fatal with a clear log | runtime only: decode the exception in `RaiseException`/`RtlUnwind`, then abort | **Chosen.** Every throw is an error path; the only `catch(...)` blocks are STL cleanup that rethrows or swallows; /EHs means no fault reaches a catch; both game `__try` blocks are already handled or cannot fire. |
| (b) setjmp/longjmp at try entry | lifter: a native `setjmp` at each of the 6 try-entry state writes and at every SEH4 `__try`; runtime: walk `fs:[0]`, run filters as guest code, `longjmp` to the saved host frame; funclets lifted as entries | Rejected. Costs a native `setjmp` per try entry on hot STL paths for zero observed throws. Destructor unwinding of the 1,445 frames still needs (c)'s unwinder, so (b) does not save that work. |
| (c) host `__CxxFrameHandler3` + `RtlUnwind` | runtime: dispatch over `fs:[0]`, run unwind funclets and catch funclets as lifted functions with the parent `ebp`, then `longjmp` the native stack to the frame owning the target guest `esp`; lifter: a native `setjmp` per EH frame (1,451 + 97) keyed by guest frame address; function list: every unwind action, catch handler and `__except` block (from the FuncInfo and scope tables) as an entry | Rejected for now. Large, risky, and nothing in normal play needs it. It is the upgrade path if slice 5 ever logs a throw. |

Under (a), the 9 hand-written dispatch routines and the stubs:

- `_JumpToContinuation` 0xA67B57, the catch continuation 0xA67E1B (in 0xA67DB6),
  0xA6CC52 (in 0xA6CC3E), `_EH4_TransferToHandler` 0xA7D2D8, and
  `_CallMemberFunction0/1` 0xA67B63/0xA67B6A/0xA67B71 are reached only from
  `__CxxFrameHandler3` or `__except_handler3/4`, which are reached only from
  `RaiseException`/`RtlUnwind`. Those abort first. No change.
- `__trandisp1` 0xA775F5 and `__trandisp2` 0xA77681 are x87 libm dispatch, not
  EH. They belong to the lifter lanes, not to this plan.
- The 33 + 2 C3 targets are normal-path SEH4 pieces (`__finally` calls and the
  `call __SEH_epilog4; ret` tails). They need the lift-review function-list fix
  (slice 2), whatever the EH design.

## Slices

Slices 1 and 2 edit the lifter or the function list; run them one after the
other. Slices 3 and 4 edit `runtime/src/win32_core.c`; one worker.

1. **Frame helpers bridge `ebp`** (lifter + HP lift config).
   - Engine `tools/xrecomp/lifter.py` (HP checkout): `seh_epilog` accepts a set,
     like `seh_prolog` (`SEH_HELPERS = SEH_PROLOGS | SEH_EPILOGS`, built with
     `_as_addr_set`); `SEH_EPILOG` users keep working with the minimum.
   - `tools/xrecomp/run.py:61` or `tools/pe/run_lift_pe.py`: pass
     `seh_prolog={0xA74290, 0xA6EE6B, 0xA6EE9E, 0xA6EED4, 0xA6EF0A}` and
     `seh_epilog={0xA742D5, 0xA6EF43, 0xA6EF57}` to `BatchTranslator`
     (`translator.py:3125` already takes both).
   - Check A (static): relift; every `RECOMP_ABI_CALL(0x00A74290u` /
     `0x00A742D5u` / `0x00A6EE6Bu` / `0x00A6EE9Eu` / `0x00A6EED4u` /
     `0x00A6EF43u` / `0x00A6EF57u` line is followed by
     `ebp = g_seh_ebp; /* read back`, and each helper's `return` is preceded
     by `g_seh_ebp = ebp;`. Expected: 231 call sites, 0 without read-back.
   - Check B (run): a trace at 0xA691BF shows `ebp == esp_at_0xA691B3 - 4`.
2. **SEH4 pieces lifted** (owned by lift-review, task 7). Check: no
   `RECOMP_ITAIL` or unresolved stub targets any of the 35 C-group addresses in
   `notes/lift_review.md` (33 + 0xA72C27, 0xA80B0C).
3. **`RaiseException` and `RtlUnwind` name the exception, then abort**
   (`runtime/src/win32_core.c:344-376`). For 0xE06D7363 with nargs >= 3 and
   args[0] in 0x19930520..0x19930522: args[2] is the ThrowInfo; 0 means
   rethrow; else type name = TypeDescriptor(+8) of the first CatchableType
   (ThrowInfo+12 -> array+4 -> CatchableType+4). Print code, type name, guest
   return address, and the first 8 `fs:[0]` records (next, handler). Other
   codes: print code, flags, nargs, args. Keep 0x406D1388 unchanged.
   Check: new cases in `tests/test_win32_core.c` (use `tests/quiet_abort.h`):
   a synthetic ThrowInfo for `.?AVbad_alloc@std@@` aborts with that name in
   stderr; a rethrow (ThrowInfo 0) aborts with "rethrow"; 0x406D1388 still
   returns and pops 16 bytes.
4. **Unhandled filters** (same file). `SetUnhandledExceptionFilter` stores the
   pointer and returns the previous one; `UnhandledExceptionFilter` prints the
   record's code and returns 1. Skip if a slice already implements them.
   Check: in `tests/test_win32_core.c`, set 0xA769C8 then 0 returns 0, then
   0xA769C8; the filter returns 1 for a record with code 0xC0000409.
5. **Proof that no throw happens in play.** Boot run to the menu and into a
   level. Check: the log has 0 `RaiseException` or `RtlUnwind` abort lines and
   one `SetThreadName` line per named thread. A throw here is the trigger to
   plan option (c).

## Open questions

- 0x9D45B0 and 0x9D4800 are identified by shape only.
- No run has shown whether any throw site is reached; slice 5 answers it.
- Owner of the CRT FuncInfo 0xDFDE20 was not found (no `push`/`mov` of its
  thunk); it is CRT EH code, so it does not change the decision.
- Whether a host fault handler (SIGSEGV/SIGILL) exists in the HP runtime to
  print the guest state was not checked.

## Lead decisions (2026-10-24)

- Slice 3 done in the core slice: `RaiseException` (except the thread-name
  code 0x406D1388) and `RtlUnwind` print the code, the C++ type name for
  0xE06D7363, and the guest `fs:[0]` chain, then abort
  (`runtime/src/win32_core.c:364-486`, `tests/test_win32_core.c:329-445`).
- Slice 4 dropped. `SetUnhandledExceptionFilter` already stores the filter
  and returns the old one (`runtime/src/win32_mem.c:1768-1771`).
  `UnhandledExceptionFilter` keeps printing the exception code and address
  and aborting (`win32_mem.c:1774-1783`). Returning 1 would only let the CRT
  reach `TerminateProcess` with less information; under option (a) the
  abort with a report is the intended result.
