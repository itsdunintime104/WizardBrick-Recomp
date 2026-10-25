# Lift review: unset-flag reads, indirect tail jumps, unresolved call stubs

Lift reviewed: engine MacRecomp f2f0e46, 21,542 functions
(`generated_f2f0e46_19k/`, input list `inputs/functions.json`, 19,466 entries).
Full lift (51,670 functions): NOT finished when this review was written
(`lift3.log` has no `num_chunks`). Counts below are for the 21,542 lift only.
Where noted, membership was checked against the full lift's input list
`generated.nosync/inputs/functions.json`, which already exists.

Counts confirmed: 18 `if (_flags /* ` reads, 98 `indirect tail jmp`, 48 stubs.

## Summary

| Group | Class | Count | Fix |
|---|---|---|---|
| A | sbb followed by a signed jcc (`jg`) | 7 | Lifter: `_make_condition` adc/sbb branch, `lifter.py:908-917`, handles only je/jne/js/jns. Add OF for sbb/adc (snapshot dst and src before the write; OF = ((dst^src)&(dst^res)) sign bit), then jg/jl/jge/jle from ZF/SF/OF and jb/ja from `_cf`. |
| A | Flags passed across a call into a CRT x87 libm entry | 7 | Lifter: these entries read ZF set by the callers' helper `__fload_withFB` (0xA777A5) or 0xA777E8. On every return path of both helpers ZF = ((eax & 0x7FF00000) == 0x7FF00000) and eax is unchanged up to the jcc. Rule: for a function whose entry block reads ZF before any write, take ZF from the helper contract (or publish a thread-global ZF at the helpers' `ret` and read it at these entries). |
| A | False function start inside a prologue | 2 | Function list: drop 0x718B74 and 0x7355C3 (add to `recon/drop_function_starts.txt`). No call, jmp or absolute reference to either was found. Both are still starts in the full list. |
| A | Join of a `cmp X,0` state and a `test m,imm` state | 1 | Lifter: `_merge_flag_states` needs one operation on all edges; `normalise_zero_test` (`lifter.py:1345`) turned `test ecx,ecx` into `cmp ecx,0`, so it cannot merge with a real `test`. When merging with a test edge, express `cmp X,0` as `test X, <all-ones mask>` (same ZF/SF/PF, CF=OF=0). |
| A | Switch-table data decoded as code | 1 | Function list: 19k end 0xA2CB97 overruns the table at 0xA2CB60. The full list already ends the function at 0xA2CB60. Harmless after the full list. |
| B | `jmp [IAT]` import thunk (6-byte function) | 14 | Lifter: in `_lift_jmp` (`lifter.py:2922`), when the operand is `[abs]` and abs is an IAT slot, emit the import tail call the same way `call [IAT]` is lifted. Not confirmed whether `RECOMP_ITAIL(MEM32(slot))` works at run time; it depends on what the loader writes into the IAT. |
| B | `jmp [IAT]` tail call at the end of a game function | 6 | Same lifter rule as above. |
| B | `jmp [.data pointer]` thunk | 7 | Harmless: legitimate computed tail call; pointers start at 0xA41A64 / 0xA46218 (function entries). |
| B | C++ virtual tail call (`mov r,[obj]; jmp [r+off]` or via a register) | 48 | Harmless: legitimate; needs run-time dispatch to a function entry. |
| B | Register tail call, load not visible in the last 4 instructions | 10 | Unconfirmed; most likely virtual or function-pointer tail calls. |
| B | Run-time function-pointer table in .data | 1 | Harmless: `mov eax,[esi*4+0xF7F1E8]; jmp eax`. |
| B | Byte-map switch with one arm, rejected by the lifter | 1 | Lifter: `_analyze_switch_table`, `lifter.py:2860` (`if len(inside) >= 2`). Accept one arm when the jmp is preceded by `cmp idx,N; ja` and `movzx idx, byte [idx+map]` whose bytes all index inside the table. |
| B | Hand-written CRT / EH dispatch | 9 | Needs runtime EH support: these jump to continuations and funclets that are not function entries. Not a lifter-table fix. |
| B | Junk or unlocated | 2 | 0xA2CB33 is decode of switch-table data (see A). 0x6C1588 in sub_006C1580: jmp not located (unconfirmed). |
| C | Target is a function start in the full list | 8 | Function list: fixed by the full list. |
| C | Target is inside a full-list function, not its start | 5 | Function list / lifter: still unresolved in the full lift. Jumps cross from one piece of a Ghidra function into another piece. |
| C | Target is inside a Ghidra function's body ranges, but no lifted function covers it in either list | 33 | Function list: the front end does not cover all Ghidra `ranges`. Best fix: lift every range of one Ghidra function into one C function so the jumps between pieces become gotos (SEH4 pieces share the frame). Minimum fix: add each uncovered range start as a function start. |
| C | Target in no Ghidra function | 2 | 0xA72C27, 0xA80B0C: real CRT code (SEH4 cleanup) that Ghidra did not cover. Add as starts or merge into the neighbouring function. |
| C | Data address or junk | 0 | All 48 targets are in .text and decode as real instruction starts. |

## Most dangerous for correctness

1. **0x0096D7E4, one-arm switch.** `cmp ecx,6; ja 0x96D7F1; movzx ecx, byte [ecx+0x96D82C]; jmp [ecx*4+0x96D828]`. The table has one entry, 0x0096D7EB, and the map is seven zero bytes. The lifter rejects it (`lifter.py:2860`), so every index 0..6 becomes `RECOMP_ITAIL(0x96D7EB)`, a tail call to the middle of sub_0096D690.
2. **sbb + jg, 7 sites.** The 64-bit compare `sub lo; sbb hi; js; jg` compiles to `_flags`. The docstring at `lifter.py:1352-1356` says nothing assigns `_flags`, so the branch is never taken. The compare result is silently wrong.
3. **False starts 0x718B74 and 0x7355C3.** They cut `cmp eax,ebx` from its `je`. The real functions 0x718B60 and 0x7355B0 fall into them, and the null-argument `je` is never taken. Both starts are still in the full list.

Broad risk: group C3 (33 targets) and the B CRT/EH group. They make SEH4 epilogues, `__finally` blocks and C++ catch continuations unreachable or stubbed.

## A. Unset-flag reads (18)

All 18 are reachable from the function entry and decide a branch.

| Function | jcc VA | Original instructions | Root cause |
|---|---|---|---|
| sub_007CEEC0 | 0x7CEF1B | `sub ecx,eax; mov eax,ebx; sbb eax,edx; js 0x7CEF4A; jg 0x7CEF21` | sbb + jg, lifter gap (`lifter.py:908`) |
| sub_007D30C0 | 0x7D31D4 | `... sbb edx,[esp+0x24]; mov [esp+0x2C],edx; js 0x7D3196; jg 0x7D31DA` | sbb + jg |
| sub_007D30C0 | 0x7D3247 | `sbb ecx,edx; mov [esp+0x2C],edx; js 0x7D328F; jg 0x7D324D` | sbb + jg |
| sub_007D5AD0 | 0x7D5CDC | `sbb ebp,edx; mov [esp+0x1C],ebp; js 0x7D5CA4; jg 0x7D5CE2` | sbb + jg |
| sub_00A73158 | 0xA73421 | `pop ecx; sbb edx,ecx; js 0xA7342D; jg 0xA73427` | sbb + jg |
| sub_00A946FD | 0xA94752 | `sub esi,..; sbb edi,edx; js 0xA94818; jg 0xA9475C` | sbb + jg |
| sub_00A946FD | 0xA947CC | `sub esi,..; sbb edi,edx; js 0xA947D2; jg 0xA947A1` | sbb + jg |
| sub_00A69438 | 0xA6943D | entry: `push edx; wait; fnstcw [esp]; je 0xA69475` | ZF from caller. Paths: 0xA6942F `lea edx,[esp+4]; call 0xA777A5` falls through; 0xA693E0 (_CIatan-style) `call 0xA777E8; call 0xA69438` |
| sub_00A694DD | entry je → 0xA69539 | `push edx; wait; fnstcw [esp]; mov eax,[esp+0xC]; je` | ZF from caller: 0xA694C0 `call 0xA777E8; call 0xA694DD`; 0xA694D4 `lea edx,[esp+4]; call 0xA777A5`. eax reloaded with the hi word: the ZF rule still holds |
| sub_00A6AF88 | entry je → 0xA6AFDB | same shape | ZF from caller 0xA6AF30 (`call` at 0xA6AF76) |
| sub_00A6B0D8 | entry je → 0xA6B12B | same shape | ZF from caller 0xA6B080 (0xA6B0C6) |
| sub_00A6E9D8 | entry je → 0xA6EA2F | same shape | ZF from caller 0xA6E980 (0xA6E9C6) |
| sub_00A6EB08 | entry je → 0xA6EB5F | same shape | ZF from caller 0xA6EAB0 (0xA6EAF6) |
| sub_00A6ED28 | entry je → 0xA6ED9C | same shape | ZF from caller 0xA6ECD0 (0xA6ED16) |
| sub_00718B74 | 0x718B88 | 0x718B60: `push ebp; mov ebp,esp; and esp,-16; sub esp,0x484; mov eax,[ebp+8]; push ebx; xor ebx,ebx; cmp eax,ebx` / 0x718B74: `push esi; ...; je 0x71AB97` | False start (Ghidra FUN_00718b74) inside FUN_00718b60 |
| sub_007355C3 | 0x7355D3 | 0x7355B0: `fldz; mov eax,[0xF717E0]; push ebx; fstp [..]; push ebp; xor ebx,ebx; cmp eax,ebx` / 0x7355C3: `push esi; push edi; ...; je 0x735734` | False start (Ghidra FUN_007355c3) inside FUN_007355b0 |
| sub_00459500 | 0x459D8A | 0x459D86 `test ecx,ecx` falls into 0x459D88 `fstp st(0); jne 0x459D95`; 0x459B56 `test dword [eax+0x84],0x100; jmp 0x459D88` | Join of cmp-form (normalised `test ecx,ecx`) and test-form states |
| sub_00A2C8A0 | 0xA2CB2B | 0xA2CB60 `70 C9 A2 00 68 CA A2 00 ...` is a jump table + byte map; decoded as `jo 0xA2CB2B`, which lands inside `push 0xED7F88` (bytes `7F ED` = `jg`) | Switch data decoded as code; 19k end 0xA2CB97, full-list end 0xA2CB60 |

Helper contract evidence: 0xA777A5 `mov eax,[edx+4]; and eax,0x7FF00000; cmp eax,0x7FF00000; je 0xA777B7; fld qword [edx]; ret`; special path ends `test eax,0` (ZF=1); `mov eax,[edx+4]; ret`. 0xA777E8 `mov eax,[esp+8]; and eax,0x7FF00000; cmp eax,0x7FF00000; je +1; ret; mov eax,[esp+8]; ret`. So ZF=1 exactly when the double is Inf/NaN. Today the branch is never taken, so Inf/NaN inputs to these math routines take the normal path.

Side finding: Ghidra marks `__fload_withFB` (0xA777A5) `no_return`, which is wrong (it returns at 0xA777B6). The lifter does not read `no_return` (no match in `lifter.py`/`translator.py`), so the lift is not affected.

## B. Unresolved indirect tail jumps (98)

| Class | Count | Sites (function: jmp VA [operand]) |
|---|---|---|
| Import thunk `jmp [IAT]` | 14 | AC26AA RtlUnwind; AC2410 DirectInput8Create; AC2416/AC241C/AC2422 XINPUT1_3 ordinals 3/4/2; A96084 D3DXMatrixMultiply; A9608A D3DXCreateEffectCompiler; A96090 D3DXCreateBuffer; A960A8 D3DXCreateTextureFromFileInMemory; A960AE D3DXCreateTextureFromFileInMemoryEx; A960C0 D3DXSaveSurfaceToFileInMemory; A960C6 D3DXGetShaderConstantTable; A960CC D3DXCompileShader; A960D2 Direct3DCreate9 |
| Tail call through IAT after `mov [esp+4],x` | 6 | 98B434, 98BFA8, 98F631, 9A784E `jmp [0x10DE9D8]` LeaveCriticalSection; 9A504C `jmp [0x10DE928]` SetEvent; 9C2734 `jmp [0x10DE9E4]` ResumeThread |
| `jmp [.data pointer]` thunk | 7 | A41AEA [0xED97BC], A41AF6 [0xED97CC], A41B08 [0xED97E4], A41B0E [0xED97EC], A41C4C [0xED9994], A41C58 [0xED99A4] (initial value 0xA41A64); A4628E [0xED9E80] (0xA46218) |
| C++ virtual tail call | 48 | A898FD [eax+4]; A6617F [eax+N]; A02F9A eax; 9640F9, 964079, 963007, 962FE3, 926F00, 8CF4F5, 776C64, 6C757C, 579D33, 570003, 56829E, 552386, 51BAFA, 51AE23, 5194DE, 44C0DE, 5C2DA7 (eax); 962908, 939343, 939323, 939200, 907E92, 8F5403, 8A61CC, 7E07D0, 78BD50, 7783CB, 776C44, 776C29, 776C24, 6F12C0, 6C1570, 6A74FD, 5E135D, 5B4E46, 59C0DA, 599AB1, 5997CF, 5523E2, 54B01F, 51960E, 51945E, 513D9F, 4DC54E, 4C40F5 (edx) |
| Register tail call, load not in last 4 instructions | 10 | 78E555, 76A16C, 76A14C, 6F255D (eax, after `mov [esp+4],ecx`); 6F26FC (ecx); 536130, 51B8A9 (edx, after pops); 7E10A9, 6B57F3 (eax); A30219 (eax, after `add esp,N`) |
| Run-time function-pointer table | 1 | 8B53C2 `mov eax,[esi*4+0xF7F1E8]; jmp eax` (.data, not code addresses at rest) |
| Byte-map switch, one arm | 1 | 96D7E4 `jmp [ecx*4+0x96D828]`, map 0x96D82C (7 x 00), sole arm 0x96D7EB |
| CRT / EH hand-written | 9 | A775F5 `__trandisp1` and A77681 `__trandisp2` (`jmp [ebx]`, x87 libm dispatch tables); A7D2D8 `_EH4_TransferToHandler` (`jmp esi`); A67B63 `_CallMemberFunction0`, A67B6A and A67B71 `_CallMemberFunction1` (`pop eax; pop ecx; xchg [esp],eax; jmp eax`); A67B57 `_JumpToContinuation`; A67E1B in FUN_00a67db6 (`mov esp,[ebx+N]; mov ebp,[ebx+N]; jmp eax`, catch continuation); A6CC52 in FUN_00a6cc3e (`pop ebp; jmp eax`, unnamed) |
| Junk / unlocated | 2 | A2C8A0 at 0xA2CB33 (switch data decode); 6C1580 at 0x6C1588 (jmp not found within 400 bytes before a ret; unconfirmed) |

The lifter text has no VA on these lines. VAs were found by disassembling from the nearest label (`<job tmp>/lr/b.txt`).

## C. Unresolved call stubs (48)

Almost all are reached by a jcc or tail jmp that leaves the current lifted function (`if (...) { RECOMP_ABI_CALL(0x..., sub_...); return; }`), not by a `call`. Three are true calls: 0xA6F13C, 0xA6F017, 0xA6D4A6.

| Class | Targets |
|---|---|
| Function start in full list (8) | 699380, A6D4A6, A6F017, A6F13C, A72C35, A7AED8, A80AEB, A80B10 |
| Inside a full-list function, not its start (5) | A6AED7 (in A6AE9C-A6AEEB, Ghidra FUN_00a6ae60), A72C44 (in A72C35-A72C4A), A77910 (in A77907-A77943), A7C968 (in A7C95D-A7C97C), A91411 (in A913F3-A91417) |
| In Ghidra ranges, covered by no lifted function (33) | 5949B0 (FUN_00594a00), 757560 (FUN_00759720; starts `push -1; mov eax,fs:[0]`, may be a real function), 7F5580 (FUN_007f5640), 826280 (FUN_008297c0), 8A85B9 (FUN_008a8530, shared epilogue `pop edi; pop esi; xor eax,eax; pop ebx; ret`, 5 refs), A678E0 (FUN_00a678f8), A6A741, A6A742, A6A76A (FUN_00a6a6e2), A6D46F (FUN_00a6d393), A6D658 (FUN_00a6d5a5), A6DBF2, A6DBF6, A6DC19 (FUN_00a6da6f), A6DF51, A6DF54, A6DFD8, A6DFDD, A6DFEF, A6DFF1 (FUN_00a6de04), A6E291, A6E2A2, A6E2A4 (FUN_00a6e18c), A6F7AA (FUN_00a6f728), A79ADB (FUN_00a79aed), A7A9E0 (FUN_00a7a9f6), A7ADEC (FUN_00a6ad40), A8119C, A811BD, A811C9, A8123C, A8124B (FUN_00a810c0), A91386 (FUN_00a911ff) |
| In no Ghidra function (2) | A72C27 (`push edi; call 0xA71CC7`), A80B0C (`and [ebp-0x20],0; mov eax,[ebp-0x20]; call 0xA742D5`) |

Many C targets are SEH4 code: `call 0xA742D5; ret` (__SEH_epilog4) and `mov [ebp-4],-2; call <finally>`. Per-target listing with bytes: `<job tmp>/lr/c.txt`.

## Not confirmed

- Full-lift counts (lift not finished).
- Whether `RECOMP_ITAIL` of an IAT value or of a mid-function address dispatches or traps at run time (runtime not read).
- The jmp for the 0x6C1588 site, and the register loads for the 10 register tail calls.
- Whether 0x757560 is a real function entry.
- The exact front-end code that drops Ghidra non-entry ranges (the effect is shown above; the front-end source was not read).

Tools: `<job tmp>/lr/` (`lr.py` info/dis/xref/stubs, `b.py`, `run.sh`).
