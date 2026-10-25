# Stutter ledger (2026-10-24)

Tool: `HPRECOMP_FRAME_TRACE=1` (runtime/src/frame_trace.c, format in
runtime/include/frame_trace.h) times every COM method and every import; report
with `tools/perf/ft_report.py TRACE --phase NAME:T0-T1`. Runs: hp-perf-build
lhp_boot, tools/boot/load_kb.input, private HOME, real audio (no
SDL_AUDIO_DRIVER=dummy), 335 s. Phases: load 0-45 s, cutscene 45-235 s,
gameplay 235-336 s. Traces in the job tmp `perf/` folder (base1, warm1, samp2).

| Run | Phase | Frames | Median ms | p99 ms | >16.7 | >33.3 |
|---|---|---|---|---|---|---|
| base1 (DXVK cache cold, load avg 110-217) | cutscene | 21,564 | 8.27 | 25.42 | 679 | 20 |
| base1 | gameplay | 11,347 | 8.27 | 17.44 | 414 | 5 |
| warm1 (DXVK cache warm, load avg 46-116) | cutscene | 30,619 | 5.43 | 17.73 | 597 | 27 |
| warm1 | gameplay | 11,969 | 8.04 | 17.64 | 477 | 14 |

## Attribution of frames over 33.3 ms (Present thread)

- Inside IDirect3DDevice9::Present: base1 35 of 44 (load + cutscene + gameplay),
  warm1 45 of 59. The thread's CPU time in those frames is 3-8 ms; it is blocked,
  not descheduled (rule `offcpu`: 1 frame in two runs).
- USER32 PeekMessageA (SDL event pump): 3 + 8 frames.
- Guest CPU (`guest`): 4 + 4 frames, at the cutscene-to-gameplay load (234 s,
  259 s), where other guest threads wait 10-30 s in WaitForSingleObject.

## What Present waits on (samp2: `/usr/bin/sample`, 44-54 s, 7546 samples)

- dxvk-submit: 6033 samples in `DxvkSubmissionQueue::submitCmdLists` ->
  `Presenter::presentImage` -> KosmicKrisp -> `-[CAMetalLayer nextDrawable]`
  (5985). The submit thread waits for the compositor to return a drawable.
- dxvk-cs: 6435 samples idle in its condition wait. Pipeline compiles
  (`DxvkGraphicsPipeline::createInstance` 302, `createOptimizedPipeline` 278)
  are about 4% of that thread.
- The game thread then waits in D3D9SwapChainEx::SyncFrameLatency
  (dxvk src/d3d9/d3d9_swapchain.cpp:1153) for the submit thread.
- DXVK log: `VK_PRESENT_MODE_IMMEDIATE_KHR`, image count 3, "Graphics pipeline
  libraries not supported".

## Hypotheses

| | Hypothesis | Verdict | Evidence |
|---|---|---|---|
| H1 | Audio (XAudio2/FAudio calls) | Rejected for frame spikes | No IXAudio2 or voice method took 2 ms or more in base1 or warm1. Audible glitches were not measured. |
| H2 | Shader or pipeline compiles | Minor | No D3DXCompileShader, CreateVertexShader or CreatePixelShader call took 2 ms or more. A warm DXVK cache did not reduce spikes (20 -> 27). dxvk-cs is idle 85% of the sampled load window. KosmicKrisp has no VK_EXT_graphics_pipeline_library and no Mesa disk cache. |
| H3 | Synchronous file I/O | Rejected | ReadFile at 2 ms or more: 16 (base1) and 20 (warm1) calls, at most 6.5 ms, all on loader threads, never on the Present thread. Game data is in the wine prefix and saves are in ~/Library; neither is in iCloud. |
| H4 | Presentation | **Cause** | Present spikes are dxvk-submit blocked in CAMetalLayer nextDrawable (above). |
| H5 | Other processes | Contributes through H4 | The machine load (load avg 46-217 with about 330% CPU used: threads in I/O wait; WindowServer 15-49%) changes the median (8.27 vs 5.43 ms in the cutscene) and makes drawable returns late. The game thread is not descheduled. |
| H6 | Guest heap or locks | Rejected on the Present thread | EnterCriticalSection at 2 ms or more: 42 + 54 calls, all on other guest threads. |

Fix: belongs to presentation (CAMetalLayer drawable acquire in KosmicKrisp and
the DXVK present path). Not changed here.
