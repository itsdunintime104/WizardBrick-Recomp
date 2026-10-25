# Shader hitches at load start (1-3.4 s Present gaps)

Status: diagnosis 2026-10-24; game-cache reset found and Mesa cache measured 2026-10-25. Source: DXVK v3.1.1-157-g04e66af0 (`~/Developer/hp-dxvk/dxvk`),
KosmicKrisp 26.2.5 (`~/Developer/hp-mesa/src/mesa-26.2.5`).

## Symptom
Reliability run val4 (480 s, `.app`): frames of 3443, 1553, 1485, 1076, 1622 and 1324 ms at
58.3, 59.8, 133.5, 221.3, 252.1 and 277.9 s (the 122-125 s and 421-424 s gaps sit on the
vmmap/heap samples and are not counted). In each, almost all time is in the host Present path,
at a load start, while the loader thread calls D3DXCompileShader.

## Where Present waits
- `d3d9/d3d9_swapchain.cpp:1155`: Present waits on `frameLatencySignal` for frame
  `frameId - maxFrameLatency` (2 in the `.app`). The signal fires only after the CS thread has
  recorded and submitted that frame.
- DXVK log: `Graphics pipeline libraries not supported` (`dxvk/dxvk_pipemanager.cpp:180`);
  KosmicKrisp does not expose VK_EXT_graphics_pipeline_library (it is absent from the
  enabled extension list).
- Without GPL, the first draw with a new state vector compiles the full pipeline on the CS
  thread: `DxvkGraphicsPipeline::getPipelineHandle` (`dxvk/dxvk_graphics.cpp:1104`) ->
  `createInstance` (`:1218`), where `canCreateBasePipeline` is false (`:1246-1249`, no GPL), so
  `getOptimizedPipeline` runs synchronously (`:1228-1229`). The worker pool
  (`compileGraphicsPipeline`, `:1129`) is used only when a GPL base pipeline exists.
- In KosmicKrisp each `vkCreateGraphicsPipelines` runs `kk_compile_graphics_pipeline`
  (`vulkan/kk_shader.c:1202`): two `mtl_new_library` calls from MSL source (`:1227`, `:1238`)
  and `mtl_new_render_pipeline` (`:1290`), i.e. `newLibraryWithDescriptor`
  (`bridge/mtl_compiler.m:59`) and `newRenderPipelineStateWithDescriptor` (`:217`).
- So the CS thread stalls for the sum of the new pipelines at a load start, and Present waits
  behind it at `d3d9_swapchain.cpp:1155`.

## Options that exist (none makes the compile asynchronous without GPL)
`dxvk.enableGraphicsPipelineLibrary` (`dxvk/dxvk_options.cpp:9`; Auto, cannot turn on a
missing extension), `dxvk.numCompilerThreads` (`:8`; workers only help GPL or shader
prep). There is no `d3d9.*` or `dxvk.*` option for async or deferred pipeline compile in
this version, and no state cache (the pipeline state cache was removed upstream).

## Caches
| Cache | Content | Location | Keyed on HOME? |
|---|---|---|---|
| DXVK shader cache | DXVK internal shader IR, not pipelines (`dxvk/dxvk_shader_cache.h:20`) | `$HOME/.cache/dxvk/<hash>.dxvk.{bin,lut}` (`dxvk_shader_cache.cpp:665`) | yes |
| Mesa disk cache | none: KosmicKrisp never calls `disk_cache_create` (only frees it, `kk_physical_device.c:917`), so `MESA_SHADER_CACHE_DIR` / `_DISABLE` do nothing. A patch that adds it saves little (below) and is not shipped | - | - |
| KosmicKrisp VkPipelineCache | in memory; serializes MSL text (`kk_shader.c:1429`), and deserialize recompiles MSL to Metal | - | - |
| Apple Metal compiler cache | MSL -> AIR (`libraries.*`) and AIR -> GPU (`functions.*`) | `$(getconf DARWIN_USER_CACHE_DIR)/local.harrypotterrecomp/com.apple.metal/` for the `.app`; `.../com.apple.metal/` for a bare binary | no |

A fresh HOME resets only the DXVK IR cache. The Metal cache sits outside HOME, is per user and
persistent, and was already warm for val4 (written since 06:06 that day).

## Measurement (2026-10-24, incomplete)
Runs `shk_out/cold` and `shk_out/warm` (`$TMPDIR/shk_out`): `tools/check/reliability.sh --short
--skip-tests` on `~/Developer/hp-boot-build/lhp_boot` with `VK_DRIVER_FILES` = the
`hp-mesa-install2` ICD and `DXVK_CONFIG="d3d9.maxFrameLatency = 2"`; the warm run used a copy of
the cold run's HOME (`--out` dir with `home/` already present, no new flag). Both ran 480 s,
no "D3D9 window hidden".
- No gap numbers: that `lhp_boot` (built 10:12) has no `HPRECOMP_FRAME_TRACE` support
  (`strings` count 0; the `.app` lhp has it), so both runs report "no frame trace".
- The warm run did not grow `$HOME/.cache/dxvk/*.dxvk.bin` (2,140,416 B in both), so every
  shader IR came from the cache.
- The `.app` launcher sets `VK_DRIVER_FILES` unconditionally (`launcher.sh.in`), so a caller's
  driver choice is ignored for the `.app`; the patched driver reaches the `.app` only through
  `make_app.sh` `KK_PREFIX`.

## Game shader cache reset (2026-10-25): the main cause in test runs
The game keeps its own cache of compiled HLSL in
`<save dir>/Users/Player/AppData/Roaming/WB Games/LEGO® Harry Potter™/CachedShaders`
(`VERSION.DAT` plus one `.DAT` per shader). `d3dx_game_cache_check` (`runtime/src/d3dx_shader.c:1432`)
compares `RECOMP_D3DX.TXT` in that folder with `D3DX_COMPILER_REVISION` (`d3dx_shader.c:1193`). When
they differ it zeroes `VERSION.DAT`, so the game wipes the cache and recompiles every shader
(D3DXCompileShader through vkd3d) during play. It then writes the new stamp.

- This is correct behaviour: a cache from another compiler revision may hold wrong code. In a player's
  profile it fires once per compiler change, then the cache stays warm.
- Every test run before 2026-10-25 copied a seed profile whose stamp was missing or old, so every run
  reset the cache (game.log: "d3dx9: compiler changed, game shader cache marked stale"). Test runs
  therefore overstate the stutter a player sees.
- Check: gpu1 cold then warm in one HOME. Cold: 335 cache files before, 357 after, stamp written.
  Warm: no "marked stale" line.
- Owner play on gpu1 (84d4f6e) with a warm cache, no input script, no `RECOMP_ASSUME_FOCUS`
  (frame log, 11.3 min of play at 120 fps): 0.09 held frames/s. No hold over 100 ms during play: five
  in the first 1.1 min (boot, longest 292 ms) and one when the owner returned to a hidden window, all
  display-side. Owner: "this one performance is much much better". The scripted cold-cache gpu1 run
  (h13 base2) had 3.10 held frames/s and a 2.25 s game-side hold.
- Not yet separated: warm cache vs no input script vs no focus override. Next check: one build, same
  seed, cold and warm cache, scripted, compared with `an2.py`.

## Mesa disk cache measured (2026-10-25)
A patch adds `disk_cache_create` in `kk_physical_device_init_pipeline_cache` and the device
`mem_cache` (DXVK passes `VK_NULL_HANDLE`, and `vk_pipeline.c:2255` falls back to `mem_cache` only when
it exists). Warm runs hit 20/20. But SPIR-V -> NIR -> MSL costs about 0.25 ms per pipeline, while a
Metal compile Apple has not seen costs about 15 ms (about 98% of the cold cost), and Apple's per-user
Metal cache already hits across launches. Not shipped.
