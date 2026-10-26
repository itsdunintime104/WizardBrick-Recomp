# Unresolved call stubs of the full lift (885)

Status: PARTIAL. Classes (a), (b) and (e) are counted for all 885. The (b) residue (294) is not resolved; see "Open".

Scripts and evidence: `<job tmp>/stub_triage/` (`index.py` call sites, `rd.py`/`rd3.py` CFG walks, `dry.py` dry run, `classes.json` class per stub, `cand_drop_starts.txt`, `cand_add_starts.txt`).

## Main finding

No stub is reached by a `call`. Every reference is a jcc, a direct `jmp`, or a fall-through that leaves the lifted function (`index.py` over `generated.nosync/lhp_0*.c`). A recursive descent from the caller's start reaches the target for 880 of 885 (`rd.py`). So the stubs are not missed entries. They are the bodies of the callers, cut short by false function starts.

The lifter lifts `[start, end)` from `inputs/functions.json`, and `_functions()` in `tools/pe/analyze_pe.py` ends each function at `min(ghidra max + 1, next start)`. A false start inside a body ends the body there. Each jump past the cut becomes `RECOMP_ABI_CALL` to a stub that does nothing.

The translator has its own ownership recovery (`discover_cfg_ownership`, `MarvelRecomp/engine/tools/xrecomp/translator.py:1228`). It never runs for this PE: it needs strong entries (`has_prologue`, `called_by`), and `emit_inputs.functions()` emits only `start, end, name`.

## Classes

| Class | Count | Meaning |
|---|---|---|
| (a) entry missing | 2 | ILT thunk target with no start: 0x535240 (thunk 0x4012EE), 0x474550 (thunk 0x40234C) |
| (b) extent wrong, fixed in dry run | 588 | Owner's CFG reaches the target; every start between owner and target is a non-entry; drop them and merge their bodies |
| (b) blocked by a start with entry evidence | 251 | A start between owner and target has a direct call (38) or a pointer/thunk dword (213) |
| (b) backward jump, owner not found | 33 | Target is before the caller; caller is a false start; no earlier real entry was found whose CFG reaches both |
| (b) merged extent still short | 10 | The owner's Ghidra bodies end before the target (e.g. 0x498DC0: `jmp 0x498DC0` at 0x498DBC, but Ghidra never decoded 0x498DC0) |
| (c) caller is data | 0 confirmed | See "False starts" below |
| (d) CRT/SEH funclet | 0 as its own class | CRT targets (0xA8119C in `__alloc_osfhnd` etc.) reach through the normal CFG; they sit in (b) |
| (e) not reached | 1 | 0x6E654D: no descent from any caller or earlier start reaches it |

## False-start kinds found (evidence)

1. Loop-alignment padding. MSVC jumps over padding to a 16-aligned loop head: 0x42FFB2 `jmp 0x42FFC0`; 0x42FFB4 `8d a4 24 00 00 00 00` (lea esp,[esp]); 0x42FFBB `eb 03`; 0x42FFBD `8d 49 00`. Ghidra ends FUN_0042ff80 at 0x42FFB3 and makes FUN_0042ffb4 (padding) and FUN_0042ffc0 (the loop). The loop's exit jumps (0x42FFAA `jle 0x42FFED`) become stubs. Same shape: 0x4331F4/0x433200, 0x464954/0x464960, 0x43D727. 123 padding fragments are in the drop candidates.
2. Switch arms as functions. 0x49E1EC `jmp [eax*4+0x49E7C0]` ends sub_0049E1B0 (`RECOMP_ITAIL`). Each arm (0x49E1F3, ...) is a function because its address is in the table. Arms jump to joins (0x49E7B6, 0x49E2E0) that are stubs. `recon/switch_tables.json` has `switch: 0`: no table is resolved by the front end.
3. Mid-instruction starts. 0x498DC3 is inside `mov edx,[0xF72448]` at 0x498DC0; 0x4C0868 inside `test ah,5` at 0x4C0866; 0x4B6925 inside `mov eax,[0xE28A4C]` at 0x4B6921. The lifter decodes these from the wrong byte. 13 such starts. These callers are junk decodes, but the bytes are code of the owner, not data, so they go in the drop list, not class (c).

## Fix (proposed, not applied)

1. Drop the non-entry starts. Candidate list: `cand_drop_starts.txt` (2051 starts). Rule: a start between the owner and a stub target, inside the owner's CFG (or mid-instruction, or pure padding), with no direct call, no pointer dword and no ILT thunk. Remove 0x4B7A12, 0x6E619E and 0xA6F78E first: other functions jump to them.
2. `analyze_pe.py` `_functions()`: when a function's dropped successors follow it, end = `min(next kept start, max(own max, dropped max) + 1)`. About 6 lines; it needs the dropped rows passed in. Not edited.
3. Read `recon/add_function_starts.txt` in `analyze_pe.py` (written, 2 entries).

Dry run (`dry.py`, the rule simulated in Python, `recon/analysis.json` not touched): 588 of 885 targets fall inside their owner; 2 more become starts when added. Total 590. Function count 51670 to 49412.

## Open

- 251 blocked: the evidence test for "real entry" scans every byte offset for the dword, so coincidental values count. Example: 0x474953 has evidence, yet 0x4748E0 falls into it and 0x474953 jumps back to 0x474910. Use 4-aligned pointers outside `.text` plus decoded immediates, then rerun `dry.py`.
- 33 backward and 10 short: need a CFG extent (descent from the owner) instead of Ghidra max. That is more than 20 lines in `analyze_pe.py` (needs capstone).
- Not checked: translator-added functions (1851) after the drops; whether the lifter resolves the switch tables once the arms are inside the owner.

## Addendum: caseD_* / __break starts (6797)

- Evidence (`cases.py`): no direct call and no ILT thunk targets any of them. 332 have a 4-aligned dword in a non-code section; 6465 have no evidence.
- Owner check: for 3653, the CFG walk from the nearest preceding non-label start (following jump tables) reaches the label. These are confirmed internal labels. For 3144 the walk does not reach it. The nearest start is often a false start itself, so this naive owner search is not enough. Not confirmed.
- 0x417325 and 0x53575F are not real entries. Both are 0xCC padding: 0x417320 `movzx eax, byte [ecx+4]; ret`, then 0x417325 `int3`; 0x53575F follows the `ret` at 0x535757 and is `int3`. A dword equal to each exists in data, but no code can start on int3 padding. The vtable reading in notes/names.md is wrong or points one byte off; not checked. They are not added to `add_function_starts.txt`.
- 0x98A670 is a start in `generated.nosync/inputs/functions.json` (end 0x98AB2A).
- 0xA37730 is added to `recon/add_function_starts.txt` (thread start; evidence in the file).

`analyze_pe.py` change: in `analyze()`, besides `drop`, drop every Ghidra row whose name starts with `caseD_` or equals `__break`, unless its address is in a keep set: direct call targets, ILT thunk targets, or 4-aligned pointers in a non-code section. The keep set needs a byte scan (calls and thunks need decoding), so it is more than 20 lines; not edited. Then merge the dropped bodies into the preceding kept function, as above.

Dry run (`cases.py`; counting rule: target is a start, or lies in its owner, or in the function that absorbed a dropped owner):

| Variant | Functions | Stubs fixed | Stubs left |
|---|---|---|---|
| Current lift | 51670 | 0 | 885 |
| Stub drops + merge | 49412 | 602 | 283 |
| Plus 3653 confirmed label drops | 46972 | 611 | 274 |
| Plus all 6465 labels without evidence (not confirmed safe) | 44616 | 639 | 246 |

The 602 is above the earlier 590 because this rule also counts targets whose owner was merged into an earlier function.

## Rebuild result (CFG function list)

Status: `tools/pe/analyze_pe.py` builds the function list with `find_functions`. `tests/test_analyze_pe.py`: 4 passed. Pyright: 0 errors. The lift of this list is running: PID 43103, log `<work>/hp-lift-cfg/lift.log`, report `<work>/hp-lift-cfg/generated/lift_report.json`. Engine: `<work>/MacRecomp-hp-int` (feat/pe-integration, 800aa75). `MacRecomp-hp-review` cannot run it, because its run.py has no `--seh-prolog` and `run_lift_pe.py` now always passes that flag.

Run (about 20 s): `analyze_pe.py orig.nosync/lhp_win32.exe --ghidra-functions recon/ghidra_functions.json --ghidra-data recon/ghidra_text_data.json --drop-starts recon/drop_function_starts.txt --add-starts recon/add_function_starts.txt -o <work>/hp-lift-cfg/analysis.json`.

Rule. Ghidra starts are candidates only. A start is kept on one of these kinds of evidence:
- listed: the PE entry, an export, or an entry in recon/add_function_starts.txt;
- thunk: the target of a 5-byte `jmp rel32` thunk (ILT);
- data: an aligned dword in a data section that holds the address (never counted for caseD_/__break names);
- call: a direct call target;
- imm: a code-address immediate in code (not a switch arm);
- jump: the target of a jump from inside a span to outside it (the lifter makes it a tail call). Jumps from an orphan's span do not count;
- orphan: a candidate that no span covers and that is not fill. If the code from the previous span's end reaches it, that address is the start instead.

An orphan that kept code calls or jumps to is relabelled with that evidence. A span is [start, min(descent end, next kept start)). A start inside an instruction of a lower span is dropped and the search restarts. So is a data or immediate start that straight-line code of an entry-evidenced body runs into. Ghidra no_return flags count only if the routine has no ret.

Result: 46416 functions (before: 51670). Evidence counts: orphan 19016, call 13240, data 7685, thunk 3183, jump 2567, imm 721, listed 4. Conflicts are resolved by the jump rule: no span reaches past the next kept start.

caseD_/__break starts left: 1890 of 6797 (jump 1885, orphan 5). All are in 0xAA0000-0xABFFFF (0xAB0000: 1287, 0xAA0000: 598). That region has 2821 Ghidra caseD_ labels and FUN_00ab0749 (26 `jmp [eax*4+T]`). The jump chains start at orphan 0xAA86B6, which was later promoted. Cause not confirmed: 0xAA86B6 is not in a Ghidra data item, so "orphan inside a table" is ruled out.

Orphans: of the 20611 orphans counted in the earlier run (when jumps from jump starts were not evidence), 19213 have no call, jump, fall-through or immediate from any kept body. 811 of them match an E8/E9 rel32 byte pattern (mostly static-initialiser code at 0xC2xxxx). About 18.4k have no reference anywhere in the file. Samples (0xCC fill before each) look like real functions, not fragments: 0x53FE70 `8a442404888184040000c20400`, 0x547DF0 `33c03981900000000f95c0c3`, 0x545D10 `8b410cc3`, 0x8B7970, 0x5ADC00 `c701b4cdc500c3`.

Boot-path stubs (notes/boot_reach.md), checked against the spans. A stub check in the lifted C is still open.
- 0xA6936E: now a start (tail-call target), span 0xA6936E-0xA6937A.
- 0xA6B30A, 0xA6B363, 0xA6B3CD: inside 0xA6B22D-0xA6B3F2. Cause of the old cut: Ghidra flags `__fload_withFB` (0xA777A5) no_return, but it ends in `ret`, so the descent stopped after `call 0xA777A5` at 0xA6B305. Fixed by the no_return check.
- 0xA7A9E0: now a start, span 0xA7A9E0-0xA7A9E5. It is strstr's shared exit `lea eax,[edx-1]; pop ebx; ret`, placed before the entry and reached by a backward jump. It was missed because jumps from orphan spans did not count, and the caller was labelled orphan, not call.
- 0xA77910: now a start, span 0xA77910-0xA77943.

Earlier causes, fixed:
- Jumps from code past a span's end created starts at every label of the next function (0x4BBA60 tail-jumps into 0x4BAC00).
- A dropped start left behind the jump starts it had caused.
- ASCII strings match code addresses: "dat\0" is 0x00746164 and "ket\0" is 0x0074656B.
- Ghidra lists switch arms without their function: 0x5D3550 is missing.
- `.text` code ends at 0xC415E2, not 0xAC3787.

Scripts: `<job tmp>/funclist/`: `diag.py` (counts), `trace.py` (origin chain of a start), `show.py` (disassembly), `orphans.py`, `stubs.py` (stub classes from the lift output into `stub_classes.json`).

### caseD_ starts in 0xAA0000-0xABFFFF (2026-10-24)

The 1,885 caseD_/__break starts were all arms of one function: FUN_00aa86b6, the HRESULT-to-text routine (a compare tree on error codes, 26 `jmp [eax*4+T]` tables, and a shared exit at 0xAB45ED). Its real body is 0xAA86B6-0xAB45ED. Ghidra split it into more than 2,000 pieces; FUN_00ab0749 is one of them, a compare node right after `jmp 0xAB45ED`.

Cause: dwords in .data that are not pointers matched addresses inside this body:
- the bit mask 0x00AAAAFE at 0xE45BFC;
- 0x00AB00AA at 0xE15330, part of a run of 16-bit counts (`a8 00 a9 00 aa 00 ab 00`);
- 0x00AAD537 at 0xE3843C.

Each one became a data-evidence start that cut the span. Every jump past the cut, including each table arm, then became a jump start.

Measured: 2,164 data-evidence starts came from a .data dword with no code-address neighbour. The counter run defeated a "neighbour is any code address" rule, because 0x00A900A8 is itself in the code range.

Fix (`data_pointers`): in a writable section, a dword counts as evidence only when an adjacent dword is also a candidate start, as in a table of function pointers. Read-only sections (.rdata: vtables, SEH and EH tables) are unchanged. A candidate that loses this evidence is still kept as an orphan when no span covers it.

Result: 44,443 functions; caseD_ starts 0; orphans 20,960; jump starts 376; data starts 5,463.
- The 6 boot-path spans are unchanged.
- All 284 __xc and 6 __xi entries are still starts.
- `tests/test_analyze_pe.py`: 6 passed, including `test_hresult_switch_is_one_function`.

Engine prior art (`MacRecomp-hp-int` translator.py `discover_jump_table_entries`): the lifter inlines the arms that lie inside a function's [start, end). It makes entries only for arms past the end of a cut function. So the function list should not seed arms; it only has to cover them.

Switch tables. `recon/switch_tables.json` "switch" (passed to the lifter as reg_switches.json) holds only register-form switches (`jmp reg`). The lifter decodes memory-form tables (`jmp [reg*4+T]`) itself (lifter.py `_analyze_switch_table`). Sample lift of FUN_00aa86b6 and 0xA9684A only (`--only`, engine MacRecomp-hp-int): 229 table sites. Each one is a chain of `if (_jt == arm) goto loc_arm;` with `RECOMP_ITAIL(_jt)` only as the fallback for a value not in the table. Unhandled 0, failed 0. So HP does not need that list filled for these tables.

0x00A67B63 is `pop eax; pop ecx; xchg [esp], eax; jmp eax` (also 0xA67B6A, 0xA67B71, in switch_tables.json "other"): a jump to a return address held in a register, not a switch table. The caseD_ fix does not explain it.

Incremental-link thunks: in the current list, every 5-byte `jmp rel32` start has its target as a start (3,338 thunks, 0 targets missing; 3,183 targets carry thunk evidence). 0x40366B and its target 0x5349A0 (boot_log failure 5) are both starts, checked by a test. Failure 5 came from the list that predates the initialiser seeds. Of 3,352 Ghidra 5-byte jmp candidates, 16 are not starts; samples 0xA7B1EE and 0xA9BB17 lie inside other bodies (internal jmps), and no call reaches them.

recon/analysis.json (44,443 functions) is written by:
`<work>/HobbitRecomp/.venv/bin/python -I tools/pe/analyze_pe.py orig.nosync/lhp_win32.exe --ghidra-functions recon/ghidra_functions.json --ghidra-data recon/ghidra_text_data.json --drop-starts recon/drop_function_starts.txt --add-starts recon/add_function_starts.txt --init-tables recon/init_tables.txt -o recon/analysis.json`
It is byte-identical to `<work>/hp-lift-cfg/analysis.json`, the input of the full lift started 2026-10-24 04:17 (MacRecomp-hp-int, no lockf).

### Full lift of the CFG list (2026-10-24 04:17-04:31, MacRecomp-hp-int, analysis = recon/analysis.json, 44,443 functions)

lift_report.json: functions_in_analysis 45,883 (the translator adds its own), functions_lifted 45,883, functions_failed 0, unresolved_call_stubs 24 (before: 885), unhandled_total 20 (int 16, lcall 3, arpl 1).

The 6 boot-path addresses (0xA6936E, 0xA6B30A, 0xA6B363, 0xA6B3CD, 0xA7A9E0, 0xA77910) are not in lhp_stubs_unresolved.c. Chunks lhp_0052.c and lhp_0075.c compile at TSO=1 with build_boot.sh's flags (engine MacRecomp-hp-int includes). No chunk has the `GLD32(...) = (GLD32(` pattern that needs TSO=0.

The 24 stubs: every caller is an orphan. A jump from an orphan's span does not create a start (rule chosen to stop fragment cascades), so its out-of-span jump target stays a stub. None is data, none is an import.
- 10 targets lie in no span, 14 inside another function's span.
- CRT math: the x87 paths of _acos (0xA693A0, 0xA6AEF0, 0xA6B040, 0xA6EC90) and _pow (0xA6B190), each `lea edx,[esp+n]; call __fload_withFB`. These callers are orphans, probably because their pointers sit alone in .data (the writable-section rule).
- __open 0xA8FE74 (0xA8FF21, 0xA8FF24), _signal 0xA6D832 (0xA6DA33, 0xA6DA69), CreateCreatures 0x48F040 (0x4909A8).
- SEH4 epilog joins: 0xA6F46E, 0xA6F4A1, 0xA6F4D3, 0xA6F50E. Their callers are 0xA6F449, 0xA6F47B, 0xA6F4AE, 0xA6F4E0, and the targets sit inside a data-evidenced span.
- x87 tails inside other spans: 0xA77688, 0xA7768A, 0xA77738, 0xA7785D, 0xA7B1F3, 0xA7B284.
- Junk from a data-decoded orphan: 0xA8A7A6, 0xA8A7AA (caller 0xA8A7E3), 0x7BDAB4 (caller 0x7BDAC9).
- Boot path: none of the 24 targets or callers appears in tmp/boot/run1*.log. Stub bodies print nothing, so this is not proof that boot never reaches them.

lcall 0x969B99/0x969BA1/0x969BA9 and arpl 0xA8A7E5: data decoded as code. Each lies in an orphan span (0x969B8E-0x969BEA, 0xA8A7E3-0xA8A805) that starts 2 bytes (`8b ff`) before a Ghidra pointer table (0x0096xxxx and 0x00A8A7xx dwords). Proposed rule, not applied: drop an orphan whose descent decodes an instruction inside a Ghidra data item.

Boot's current failure (run11.log, `indirect call to unresolved VA 0x00670040`): 0x670040 was in no function. It is not a Ghidra candidate. It follows `ret; int3` fill, and `push 0x670040` at 0x6A57D9 registers it as a callback. Fix applied in `_converge`: a code immediate that points right after 0xCC fill is a start even when Ghidra missed it. Result: +28 starts (callbacks such as 0x4472D0, 0x5747F0, 0x670040, 0x6A0D90) and -9 fragments now absorbed by their owners (for example 0x670061 into 0x670040). The 6 boot spans, FUN_00aa86b6, 0x40366B and 0x5349A0 are unchanged. Tests: 8 passed. This list (44,462 functions) is at `<job tmp>/funclist/analysis5.json`; recon/analysis.json still matches the finished lift.

Orphan jumps into uncovered code (2026-10-24). The 10 stubs whose target lies in no span came from orphan callers, because orphan jumps did not count as evidence. Example: _acos 0xA693A0 `je 0xA6942F`. Its x87 path sits past the next function 0xA693E0 (which ends with `ret` at 0xA6942E), and no span covers it. Fix in `_converge`: an orphan's out-of-span jump target becomes a jump start when no kept span covers it. Jumps into another span still do not count, which keeps the caseD_ cascade out (caseD_ starts stay 0).

Effect against recon/analysis.json: +10 starts, -0, all 10 of them stub targets:
- _acos x87 paths 0xA6942F, 0xA6AF7F, 0xA6B0CF, 0xA6ED1F;
- _pow 0xA6B224;
- _signal 0xA6DA33, 0xA6DA69;
- __open 0xA8FF21, 0xA8FF24;
- CreateCreatures 0x4909A8.

The 14 left lie inside other spans: 4 SEH4 epilog joins, 6 x87 tails, 4 junk decodes. Boot spans, 0x670040 and 0x66FF60 are unchanged. Tests: 10 passed. The list is at `<job tmp>/funclist/analysis7.json`, and it is not lifted: the 04:43 lift uses recon/analysis.json (44,452 functions).
The killed 04:42 lift rewrote `generated.run11/inputs/*.json` before the move. The rest of `generated.run11` is the 04:31 run11 lift.

### The 14 remaining stubs and the 2 indirect jumps (2026-10-24, after boot run 15)

All 14 have function-list causes. They are fixed in analyze_pe.py, with tests. The list is at `<job tmp>/funclist/analysis8.json`: 44,460 functions. Against recon/analysis.json it adds 22 starts, removes 24, and changes 13 spans. caseD_ starts stay 0. The boot spans (0xA6936E, 0xA6B22D, 0xA7A9E0, 0xA77910, 0x670040, 0x66FF60, 0xAA86B6) are unchanged. All 24 stubs of the earlier lift are resolved: each target is a start or lies in its caller's span, or its caller (a junk decode) is gone. This list is not lifted yet.

- **SEH4 epilog joins** 0xA6F46E, 0xA6F4A1, 0xA6F4D3, 0xA6F50E. Example: main body 0xA6F449 (`push 8; push scope; call __SEH_prolog4; call [ebp+8]; jmp 0xA6F46E`). Filter 0xA6F461 and handler 0xA6F46B (data evidence from the scope table) cut its span, and the handler falls into the join. The main body is an orphan, so its jump into the handler's span made no start.
- **x87 tails** 0xA77688, 0xA7768A, 0xA77738, 0xA7785D, 0xA7B1F3, 0xA7B284. The CRT's hand-written x87 blocks have entries that share code: 0xA77738 is two bytes into 0xA77736 (`fstp st(0); fstp st(0); ...`). The jumping bodies were orphans.
- **Fix 1:** an orphan's jumps now count like any other function's jumps (out of its span, so the target becomes a start and the jump a tail call). The engine has no shared-tail recovery. Its unresolved stub is "a mid-function entry point" (translator.py:3777). Running off the end of a range is already a tail call (translator.py:3093). So a start is the right fix, and the owner is the function list.
- Turning orphan jumps on exposed one more cause. A pointer-evidenced false start inside an orphan was no longer dropped, because the fall-through rule trusted only entry-evidenced bodies. Example: 0x582072, inside orphan 0x581F50, split a loop at 0x582050. **Fix 2:** the fall-through rule now trusts every body.
- **Decodes of jump tables** 0xA8A7A6, 0xA8A7AA, 0xA6AED7, 0x7BDAB4, plus the lcall sites 0x969B99/0x969BA1/0x969BA9 and the arpl 0xA8A7E5. The orphans 0x969B8E, 0xA8A7E3, 0x7BDAC9, 0x781B4D and 0x9081A7 run from `8b ff` or `8d 49 00` into Ghidra pointer tables. **Fix 3** (`_runs_into_data`): an orphan whose straight-line code reaches a Ghidra data byte is not kept. Only fall-through counts, so a real switch (0x9080E0, after 0xCC fill) is kept even though its over-read arms land in data. The lcall/arpl bytes are now in no span and are not lifted.
- Merged false starts: 0x474910..0x474AAD into 0x4748E0 (0x474953 is the example this note gave as blocked) and 0x48FF10..0x4909A8 into CreateCreatures 0x48F040.

The 2 indirect jumps are not function-list causes:
- **0xA6CC52** `jmp eax` in 0xA6CC3E. eax = DecodePointer-style call 0xA73A70 on the global [0x10DAFC4]: a tail call through an encoded function pointer, known only at run time. Owner: lifter/recomp-check. It is correct as RECOMP_ITAIL if the stored pointer is a function start. Proposed: recomp-check classes `jmp reg` after a call that returns eax as a pointer tail call.
- **0xA7D2D8** `jmp esi` in _EH4_TransferToHandler (0xA7D2C1). It enters the SEH4 __except handler from the scope table. Handlers have data evidence, so they are starts (0xA6F46B, 0xA6F49E, ...). Owner: lifter/recomp-check, same class: a run-time dispatch to a listed start.

Tests: 12 passed (new: `test_shared_tails_are_starts`, `test_jump_table_decodes_are_not_starts`). Pyright 0 errors.
