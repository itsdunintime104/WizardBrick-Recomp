# Leaf fuzz, recomp-check, x87 control word (2026-10-24)

Engine: ~/Developer/MacRecomp-hp-fuzz, branch feat/pe-fuzz, commit 81399ca
(on feat/pe-integration 800aa75). Scratch: <job tmp>/leaf_fuzz/.
Analysis: recon/analysis.json (51,670 functions). recon/switch_tables.json has 0 switch tables,
so leaves with a jump table are not candidates.

## Fuzzer changes (commit 81399ca)
- tools/hobbit/fuzz_leaf.py: Image.page_ranges() merges overlapping pages (PE .data vsize runs
  over .bss); Image.code() reads from the holding segment (was: __TEXT at 0x1000).
- --x87 (opt-in): admits x87 code, FPCW 0x027F on both sides, compares fp_top and the stack as
  doubles, skips seeds with an x87 stack fault (function reads an argument from st0).
- tests/conformance/harness.py: native main also prints fp_top and st[]; ignored unless compared.
- Hobbit: test_fuzz_leaf, test_recomp_check, test_conformance, test_lift_fuzz_forms: 252 passed,
  10 xfailed. pyright 0 errors on both files.
- Not compared (same as Hobbit): EFLAGS.

## Leaf fuzz, x87-free (all 7,589 candidates; 173 s)
tested 5,833, passed 5,829, mismatched 4, skipped 1,753, compile_fail 2.
- NaN sign (x86 default NaN 0xFFC00000 vs arm64 0x7FC00000): 0x00816040 addps, 0x008CE090 subps,
  0x00905100 divps. Known class, same as Hobbit.
- Oracle artefact: 0x00A6BEFC (CRT _clrfp-style: stmxcsr; and ~0x3f; ldmxcsr; pop ecx).
  Unicorn starts MXCSR at 0, the harness at 0x1F80 (reset value). Not a lifter bug.
- compile_fail: 0x00A84300, 0x00A87790: `bts [esp + (eax>>5)*4], eax` lifts to
  `GLD32(...) = ...` (not assignable). The bts/btr/btc memory-store fix was taken out of
  feat/pe-integration (f7ce7e6); it must land.

## Leaf fuzz, x87 (--x87; all 11,157 candidates, 1,074 s, machine load ~240)
tested 8,889, passed 8,865, mismatched 24, skipped 2,254, compile_fail 2 (the same bts pair).
- NaN class, 22: x87 indefinite (sign 1) vs arm64 default NaN (sign 0) on stores
  (fstp x11, e.g. 0x00546600, 0x005620D0, 0x00584E00; fst 0x0092E6E0); SSE NaN sign (addps
  0x0053DD50, 0x00816040; subps 0x008CE090; divps 0x00905100); NaN payload on the stack
  (fld x5, e.g. 0x00546E80, 0x0082B400). The st comparison turns Unicorn NaNs into one canonical
  NaN, so payload-only st diffs are artefacts of the harness.
- 0x00907950 (seed 1, eax): fld dword of a random signalling NaN, then fstp dword. Unicorn keeps
  the sNaN (0x7FA24C82); native returns it quiet (0x7FE24C82). Intel SDM FLD quiets an SNaN when IE
  is masked, so native looks right and the oracle wrong. NaN input class; not a lifter bug.
- 0x00A8E990 (CRT _adj_fdiv, seed 0, st0): fld tbyte of random bytes; Unicorn gives NaN, native 0.
  Random 80-bit inputs include unnormals and pseudo-NaNs, which the double-backed stack does not
  model (recomp_types.h:397). Real game data passed in tbyte is valid; low priority.
- 0x00A6BEFC: the MXCSR start-value artefact above.
- Result: no wrong x87 lift found. No precision mismatch at PC=53.
Negative control (native at PC=24, Unicorn at PC=53, 200 functions): 4 of 161 mismatch by 1 ulp
(0x0043B460, 0x0044D2E0, 0x00590600, 0x0072A230). The x87 comparison is live, and the PC setting
changes float results.

## recomp-check (fresh lift with the integration engine, scratch gen/)
- failed functions 1: 0x004010E1 "no instructions decoded".
- unhandled 16: all `int 6` at 0x00A8EABD..0x00A8EC54 (ud-style traps; the check only exempts
  ud2/hlt). The stale generated.nosync had 234 (pd SSE2, cpuid, pushfd/popfd); those are fixed.
- unset-flag reads 28 reachable (same as stale lift): lhp_0008.c:38041, lhp_0010.c:22456/62638,
  lhp_0026.c:10691, lhp_0038.c:33321, lhp_0045.c:25912, lhp_0046.c:41281, lhp_0053.c:26674, ...
- unexplained indirect jumps 5: 0x00A67B63, 0x00A67B6A, 0x00A67B71, 0x00A6CC52, 0x00A7D2D8.
- decode failures 3: 0x00A687AE, 0x00A6894A, 0x00A6984A.

## x87 control word
- Lifter honours it: fldcw/fnstcw load/store g_fp_control_word (lifter.py:4311-4329); PC=24
  rounds fadd/fsub/fmul/fdiv/fi*/fsqrt (lifter.py:4114-4115, 4162; recomp_types.h:372-393);
  RC drives fist/fistp/frndint (lifter.py:4077-4087, 4209-4215; recomp_types.h:411-442).
- Game: 2,215 fldcw in 510 functions, 1,137 fnstcw, 1,103 fistp. Pattern is MSVC inline
  (int)f: fnstcw; or RC=11; fldcw; fistp; fldcw old. It keeps the current PC bits.
  5 CRT sites load the constant 0x027F (0xD20E78) around a transcendental and restore.
- CreateDevice at 0xA38272: BehaviorFlags built at 0xA3803D-0xA3808D = 0 or 0x10, |4, |0x40 or
  0x80 or 0x20. Never 0x2 (FPU_PRESERVE). DeviceType: ebx = 1 (HAL), from lea ebx,[ebp+1] at
  0xA37FA6 with ebp = 0.
- So on Windows the main thread runs at CW 0x007F (PC=24, RC nearest) after CreateDevice.
  DXVK SetupFPU: control &= 0xF0C0; |= 0x003F (hp-dxvk-headers d3d9_device.cpp:5872-5912).
- Our layer: d3d9g_IDirect3D9_CreateDevice (runtime/src/gen_d3d9_guest.c:366) passes flags to
  DXVK, which cannot change guest state; g_fp_control_word stays 0x027F (hp_boot.c:157).
- Second gap: guest threads (win32_sync.c thread_main, ~637-661) never set g_fp_control_word or
  g_mxcsr, so they start at CW 0 (PC=24) and MXCSR 0. Windows starts each thread at 0x027F/0x1F80.
- Verdict: emulate. After a successful CreateDevice without FPU_PRESERVE set
  g_fp_control_word = (g_fp_control_word & 0xF0C0) | 0x003F on the calling thread; init new
  threads to 0x027F / 0x1F80. Evidence of dependence: not proven. No game site was shown to need
  PC=24 (no Burnout-style compare loop found; not searched). The control run shows PC changes
  float results in about 2.5% of x87 leaves, so matching Windows is the safe default.
