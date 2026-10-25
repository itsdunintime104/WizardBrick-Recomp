# Template reuse: fill screen and frame stutter (2026-10-24)

Read-only research. Sources: HobbitRecomp (engine/runtime/src, README, git log),
MarvelRecomp + shared engine `~/Developer/MacRecomp-hp-int`, HP's DXVK
(`~/Developer/hp-dxvk/dxvk`, kisakcod fork, 04e66af0), Mesa 26.2.5 source
(`~/Developer/hp-mesa/src/mesa-26.2.5`), MoltenVK `MVKImage.mm` (GitHub HEAD),
NFSU2 macOS port `daniiichan/Need-for-Speed-Underground-2-macOS`
`src/host/main_macos.cpp`. [V] = read in code or doc. [I] = inferred.
Owner rule applied: each item names the existing component to reuse and the
smallest adapter.

## A. Fill screen

### What Hobbit does
- Fullscreen is SDL2 `SDL_WINDOW_FULLSCREEN_DESKTOP`, which on macOS is a
  fullscreen Space: `fe_sdl_open_window` frontend_sdl.c:245, `fe_go_fullscreen`
  frontend_sdl.c:354 [V].
- No high-DPI window: flags are `SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN`
  (frontend_sdl.c:209-210); "SDL runs without high-DPI, so points are pixels"
  (frontend_sdl.c:279-280) [V]. The drawable is the point size; macOS scales it.
- A larger render size is a CGL backing size scaled to the window
  (`kCGLCPSurfaceBackingSize`, frontend_sdl.c:224-233), not a HiDPI window [V].
- Mode list: panel pixels, 75%, desktop points, 50% (`fe_mode_sizes`
  frontend_sdl.c:139-175) [V]. Marvel needed the panel size reported as the
  screen size so the list is not cut (engine ae314d6) [V].
- Hobbit does not draw beside the notch either: its README measures
  3456 x 2169 on the 3456 x 2234 panel (README.md:113), the Space area below
  the menu-bar band [V].
- NFSU2 macOS does the same: `SDL_WINDOW_FULLSCREEN_DESKTOP` without HIGHDPI at
  the `SDL_GetDesktopDisplayMode` size (main_macos.cpp:184-221), plus a black,
  opaque NSWindow and CAMetalLayer (main_macos.cpp:231-252) [V].

### What HP already copies
- `SDL_WINDOW_FULLSCREEN_DESKTOP` in `d3d9g_backend_fit_window`
  (d3d9_backend.c:38) and `toggle_fullscreen` (win32_ui.c:309-310) [V].
- Notch-aware desktop size (`notch_points`, `d3d9g_backend_screen_sizes`,
  d3d9_backend.c:52-125) [V].
- Hobbit's four-size mode list (`listed_mode`, d3d9_guest_manual.c:680-700) [V].
- The uncommitted diff in win32_ui.c:884 drops `SDL_WINDOW_ALLOW_HIGHDPI`,
  which is Hobbit's choice [V: git diff]. Committed 1069375 had HiDPI on, so the
  stutter runs used the 4112 x 2580 drawable [I].

### Change proposal
1. Keep the Hobbit model: fullscreen Space, point-sized swap chain
   (2056 x 1290), compositor scales. Land the in-flight HIGHDPI removal.
   Reuse: Hobbit frontend_sdl.c:209-210 + 279-280 policy. Adapter: none.
2. Make the default listed mode equal the swap-chain size (2056 x 1290), so
   DXVK's blit does not stretch a 16:9 or 4:3 back buffer into 16:10 [I: DXVK
   stretches to the full swap chain with a NULL dest rect; not re-read here].
   Reuse: `listed_mode` (already Hobbit's list). Adapter: none.
3. Only if the owner wants pixels beside the notch: set
   `SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0")` before the first
   `SDL_CreateWindow` (win32_ui.c init, next to line 323). SDL2 then makes a
   borderless window over the whole screen frame (2056 x 1329) instead of a
   Space [I: SDL2 behaviour from memory, not read]. The top 39 pt sit behind
   the notch, and `notch_points` must then return 0 for the size. Hobbit and
   NFSU2 do not do this. Config-only test: one hint line.
4. Copy NFSU2's black opaque NSWindow background (main_macos.cpp:231-241) to
   hide grey flashes in the Space transition. Reuse: the objc_msgSend helpers
   already in d3d9_backend.c:44-50. Adapter: about 6 lines in
   `d3d9g_backend_fit_window` [V source, I effect].

Not confirmed: what "does not fill" looks like on screen now. Check it with a
screenshot after item 1 lands, before item 3.

## B. Stutter

### Mechanism [V]
- KosmicKrisp calls `-[CAMetalLayer nextDrawable]` inside
  `vkAcquireNextImageKHR` (`wsi_metal_swapchain_acquire_next_image`,
  wsi_common_metal.c:497; layer call wsi_common_metal_layer.m:273). It can block
  up to 1 s (`allowsNextDrawableTimeout = YES`, layer.m:165).
- DXVK acquires the next image right after each present, on the dxvk-submit
  thread (`Presenter::presentImage`, dxvk_presenter.cpp:316-324: "try to
  acquire next image already, in order to hide potential delays"). So the
  submit thread waits for a drawable immediately after it hands one to the
  compositor, and cannot submit the next frame's command lists while it waits.
  This matches samp2: 5985 of 7546 dxvk-submit samples in nextDrawable.
- Drawable count is fixed: `maximumDrawableCount = image_count`
  (layer.m:169), DXVK picks min+1 = 3 (dxvk_presenter.cpp:1303-1309), and
  KosmicKrisp caps maxImageCount at 3 (wsi_common_metal.c:62). No config
  raises it.
- `displaySyncEnabled = !immediate` (layer.m:140, 172). Only IMMEDIATE and
  FIFO exist (wsi_common_metal.c:41-43).
- MoltenVK does not call nextDrawable at acquire. Acquire only tracks image
  availability (`acquireAndSignalWhenAvailable`, MVKImage.mm:1574); the
  drawable is fetched lazily when the texture is first used or at present
  (`getCAMetalDrawable`, MVKImage.mm:1617-1627).
- Game-thread wait: `SyncFrameLatency` waits for frame N - latency, with
  latency = min(3, BackBufferCount + 1) (d3d9_swapchain.cpp:1153-1165,
  d3d9_device.h:213).
- At 120 Hz FIFO, a frame that needs 8.4 ms instead of 8.3 ms waits a whole
  extra vblank (16.7 ms). The median of 8.27 ms is the 120 Hz cap, so the
  ">16.7" counts in stutter.md are mostly single missed vblanks [I].

### Conflict to resolve first
stutter.md says the DXVK log showed `VK_PRESENT_MODE_IMMEDIATE_KHR`, but
1069375 forces interval ONE (d3d9_guest_manual.c:650-654), and
lhp_boot_d3d9.log:304 shows FIFO at 2056 x 1290. Read the "Present mode" line
of the base1/warm1/samp2 runs before ranking B4-B6. If those runs were
IMMEDIATE, nextDrawable blocked for lack of a free drawable, not on vblank.

### Candidates, ranked by expected effect

| # | Change | Layer | Code? | Reuse / adapter | Evidence |
|---|---|---|---|---|---|
| B1 | Point-sized (non-HiDPI) window: 4x fewer pixels in DXVK's present blit and KosmicKrisp's drawable blit | HP runtime | Code, in flight (win32_ui.c:884 diff) | Hobbit no-HiDPI policy, frontend_sdl.c:279-280; adapter none | Hobbit README.md:111-113: 120 FPS at 2056x1329, 84 FPS at 3456x2169 [V]; effect on HP [I] |
| B2 | 60 FPS cap for even pacing: `DXVK_CONFIG="d3d9.presentInterval = 2"` (FIFO kept; limiter at refresh/2) or `DXVK_CONFIG="dxvk.maxFrameRate = 60"` | DXVK | No: env var | DXVK limiter, d3d9_swapchain.cpp:1134-1146, options d3d9_options.cpp:44-47, `DXVK_CONFIG` config.cpp:1763; NFSU2 macOS ships `setGuestFrameLimit(60)` (main_macos.cpp:195) | [V] code; effect [I]. Trade: 60 FPS, but each frame gets a 16.7 ms budget, so 8/16 ms alternation goes away |
| B3 | Stop the acquire-after-present: delete the block at dxvk_presenter.cpp:316-324, so the acquire happens in `Presenter::acquireNextImage` (dxvk_presenter.cpp:102-109) at the next Present, as on MoltenVK | DXVK (HP fork) | Code: one block, as a 5th patch beside the 4 kisakcod patches | DXVK's own acquire path; adapter none. Gate it with an env var for A/B | [V] code; which thread runs `acquireNextImage` not traced [I]. Risk: the wait moves to the game thread, which already waits in SyncFrameLatency |
| B4 | A/B with vsync off: `HPRECOMP_VSYNC=0` gives IMMEDIATE, `displaySyncEnabled = NO` | HP env | No | existing switch d3d9_guest_manual.c:650 | [V]. Do NOT use `MESA_VK_WSI_PRESENT_MODE`: Metal configures from `pCreateInfo->presentMode` (wsi_common_metal.c:706-707) and per-present mode changes are skipped when the override is set (wsi_common.c:2865) [V] |
| B5 | Defer nextDrawable from acquire to `queue_present` in KosmicKrisp (MoltenVK model): bind the drawable and re-record the blit at present | KosmicKrisp (Mesa) | Code, larger (wsi_common_metal.c:497-509, 570-590) | MoltenVK `getCAMetalDrawable` design as the template | [V] both sources; effect [I]. Upstream-worthy; do after B3 shows the effect |
| B6 | Measure on a quiet machine (load avg was 46-217) | Test condition | No | none | stutter.md H5 [V] |
| B7 | Opaque black NSWindow + layer (NFSU2) so the compositor can skip blending | HP runtime | Code, ~6 lines | NFSU2 main_macos.cpp:231-252; objc helpers d3d9_backend.c:44-50 | KosmicKrisp already sets `opaque` from compositeAlpha (layer.m:171) [V]; any gain [I] |
| B8 | Guest-CPU spikes (4+4 frames) and PeekMessageA spikes (3+8): sample them with the shared engine's `slowframe_mark()` | HP runtime | Code: one call | `slowframe.c:151` (RECOMP_SLOWFRAME_MS, samples the calling thread's stack every 2 ms); adapter: call `slowframe_mark()` once per Present on the game thread, link slowframe.c | [V] API; Hobbit used it to find its 80-95 ms frames (aea9e8e) [V] |

Not useful here [V]: `dxvk.numBackBuffers` does not exist in this DXVK
(no option in d3d9_options.cpp); `dxvk.tearFree` only reorders
IMMEDIATE/MAILBOX/FIFO_RELAXED, none of which KosmicKrisp adds
(dxvk_presenter.cpp:1259-1286); `dxvk.latencySleep` engages the same limiter
as B2 (d3d9_swapchain.cpp:1139-1140); KosmicKrisp has no present-wait, so
DXVK's frame thread skips present waits (dxvk_presenter.cpp:2103) [V].

## C. Things HP has that Hobbit fixed differently

| Topic | Hobbit/Marvel fix | HP state | Action |
|---|---|---|---|
| Audio stalls | AUGraphStart 35-58 ms: one silent unit keeps the device running (eadc403); reuse disposed AUGraphs (a750703) [V] | XAudio2/FAudio; no call >= 2 ms (stutter.md H1) [V] | None for frame time. Audible glitches not measured |
| "Other stuff" in Hobbit's frame-rate work [V] | MP GL engine `kCGLCEMPEngine` (frontend_sdl.c:218); PBO staging for glTexImage2D (b9faf6a); buffer re-create over 16 KB (6b2497d); glCheckFramebufferStatusEXT answered locally (5d79ba6); glGenProgramsARB name pool + objc string probe skip (Marvel 5fdc170); lifter TSO plain `[ebp]` (0c0d48b); xmm locals (310ebb1); frame-dump removal from the swap (aea9e8e) | GL items have no D3D9 analogue: DXVK's CS thread already plays the MP engine role [I]. HP lift has `frame: ebp proven` markers [V]; `RECOMP_XMM_LOCAL` not found in lhp_0000.c [V grep; whether HP's lifter has it under another name: I] | Check xmm-locals in the HP lift; HP Present spikes are not guest CPU, so low priority |
| Clock | Marvel: Intel timebase 1/1 (26a067a) [V] | Windows QPC path | Not applicable |
| Occluded window | GL swap; no handling [I] | `d3d9g_present_hidden` paces Present while occluded (d3d9_guest_manual.c:753-775, in flight) [V] | Keep |
| Frame-time overlay | `hud_core.c` (renderer-agnostic: `hud_record_frame`, `hud_layout`, `hud_bar`) with a GL draw backend [V] | frame_trace.c (offline) | Use DXVK's built-in `DXVK_HUD=fps,frametimes` (env, no code) [I: not run]. Only if a Hobbit-identical overlay is wanted, add a D3D9 draw backend for hud_core |
| Event pump | Game loop pumps SDL in the frontend [V] | PeekMessageA pumps SDL on the game thread; 3+8 slow frames [V] | Sample with B8 first; NFSU2 pumps on the main thread with the game on a worker (main_macos.cpp:262-392) [V], a larger change |
