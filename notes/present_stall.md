# Present stall (2026-10-24)

Question: why do frames stall in IDirect3DDevice9::Present (see stutter.md H4), and can
configuration or the Vulkan/Metal layer fix it without changing HP runtime source.

## Runs

Build `<work>/hp-o2-build/lhp_boot` (committed -O2), vsync on (HP forces
D3DPRESENT_INTERVAL_ONE), FIFO, 3 swapchain images, 225 s, cutscene window 50-225 s.
Script: job tmp `stall/run.sh NAME SECS [ENV=...]`; stats: `stall/tools/ph.py TRACE 50 225`
(median, p90, p99, counts, Present-time histogram) and `tools/perf/ft_report.py`.

Render size: the o2 build snaps pcconfig to the full panel (ScreenWidth 4112 x 2658) at
start, even when the file says 1728x1084. All rows below render at 4112x2658 into a
4112x2580 swapchain, so they are GPU-bound (about 65 fps). Compare them with each other,
not with the display worker's vsA/vsI (1728x1084 / 2056x1290).

| Run | Setting | Frames | Median | p90 | p99 | >16.7 | >33.3 | Max |
|---|---|---|---|---|---|---|---|---|
| vs0_4112 (first try) | default | 11,843 | 15.45 | 19.10 | 20.90 | 2,800 | 27 | 1036 |
| vs0 | default | 12,028 | 11.54 | 26.68 | 36.82 | 5,160 | 645 | 164 |
| fl1 | `d3d9.maxFrameLatency = 1` | 13,185 | 15.19 | 15.94 | 16.66 | 122 | **0** | 21.7 |
| fl2 | `d3d9.maxFrameLatency = 2` | 13,130 | 15.24 | 15.99 | 16.66 | 109 | **0** | 24.8 |
| fl3 | `d3d9.maxFrameLatency = 3` | 11,049 | 16.31 | 18.23 | 21.55 | 4,782 | 19 | 500 |
| ls1 | `dxvk.latencySleep = True` (overlapped an app build: contaminated) | 11,844 | 15.56 | 20.14 | 23.17 | 3,328 | 20 | 448 |
| pi1 | `HPRECOMP_VSYNC=0` + `d3d9.presentInterval = 1` | 10,371 | 15.60 | 26.56 | 27.85 | 2,873 | 9 | 1027 |
| vsI (display build, 2056x1290, reference) | default | 20,685 | 8.33 | 8.65 | 16.73 | 211 | 7 | 265 |

ls1b (latencySleep rerun) trace: job tmp `stall/ls1b.ft`, not in this table.
Load average was 28-50 during the runs (other jobs on the machine). vs0 and fl3 should be the same
setting (DXVK default frame latency is 3), but they differ (645 vs 19 frames over 33 ms).
That is the run-to-run noise. fl1 and fl2 are two independent runs with 0 frames over 33 ms.

Not run, from code:
- `dxvk.numBackBuffers`: not an option in this DXVK (src/dxvk/dxvk_options.cpp has no such key).
- `dxvk.tearFree`: used only to pick the vsync-off mode (dxvk_presenter.cpp:1266-1274). It does
  nothing under FIFO.
- Mesa/KosmicKrisp env: the Metal WSI reads no env var. The drawable count is fixed: image count
  2-3 (wsi_common_metal.c:61-62), DXVK picks min+1 = 3 (dxvk_presenter.cpp:1306), and
  `maximumDrawableCount = image_count` (wsi_common_metal_layer.m:169). 3 is the CAMetalLayer
  maximum. `MESA_VK_WSI_PRESENT_MODE` does not reach the layer: wsi_common_metal.c:706-707 uses
  `pCreateInfo->presentMode`.

## Cause

1. DXVK presents, then at once calls `vkAcquireNextImageKHR` with timeout UINT64_MAX on the
   dxvk-submit thread (dxvk_presenter.cpp:316-324).
2. KosmicKrisp calls `-[CAMetalLayer nextDrawable]` inside that acquire
   (wsi_common_metal.c:497, wsi_common_metal_layer.m:303). nextDrawable blocks until the
   compositor gives back one of the 3 drawables. The comment there says it can block up to 1 s.
3. While dxvk-submit is blocked, the submission queue behind it waits as well. The game thread
   then waits in `SyncFrameLatency` (d3d9_swapchain.cpp:1153) for the frame
   `frameId - GetActualFrameLatency()`.
4. With frame latency 3 (default: min(3, BackBufferCount + 1), d3d9_swapchain.cpp:1159-1165),
   up to 3 frames are queued while only 2 drawables can be free (one is on screen). So the
   queue always waits on nextDrawable, and every late compositor return reaches the game thread
   as a long frame. With latency 1 or 2, the queued frames fit in the free drawables. In fl1/fl2
   the frame time stays at the GPU rate (p99 16.66 ms), with no frame over 25 ms.
   This step is inferred from the A/B results plus the counts above. I did not trace the
   drawable state per frame.

Separate defect, confirmed and not fixed: KosmicKrisp leaks one drawable per present.
`kk_encode_drawable_present` retains it (src/kosmickrisp/vulkan/kk_wsi.c:74). `kk_queue_submit`
presents it and then sets `cmd_buffer->drawable = NULL` with no release
(src/kosmickrisp/vulkan/kk_queue.c:92-96). `heap` on the live process: 3,763 live
CAMetalDrawable at 40 s, 6,417 at 85 s (about 59/s, close to the frame rate). There is one
IOSurfaceSharedEvent per drawable (3,761), and only 5 IOSurfaces, so the pool is not used up;
memory grows by about 270 B per frame plus Metal-side state. Smallest fix:
`mtl_release(cmd_buffer->drawable);` before the NULL store at kk_queue.c:96. Not built, not
measured.

## Fix (configuration only)

Not the app default: at the app's 2056x1290 size it does not beat DXVK's default
(see "Fixes (2026-10-24)" below). It helps only in the GPU-bound 4112x2658 case.

```
d3d9.maxFrameLatency = 2
```
Put it in dxvk.conf (DXVK_CONFIG_FILE) or set `DXVK_CONFIG="d3d9.maxFrameLatency = 2"`.
fl2 gives the same result as fl1 and keeps one frame of CPU/GPU overlap.

Open:
- Measure fl2 with the display build at its window size (2056x1290) against vsI (7 frames over 33 ms).
- A/B the DXVK acquire-after-present (dxvk_presenter.cpp:316-324) behind an env switch.
  Not done here: copying the DXVK tree to <work>/hp-dxvk-stall was denied by permissions.

## Reproduce

`stall/run.sh fl2 225 "DXVK_CONFIG=d3d9.maxFrameLatency = 2"`, then
`python3 -I tools/perf/ft_report.py stall/fl2.ft --phase cutscene:50-225`.
Keep the window visible. When the window is covered, nextDrawable blocks for 1 s.

## Fixes (2026-10-24)

Decisions: no frame-latency default in the launcher (the runs below show no gain at
2056x1290; the 60 fps default is a separate change). Ship the drawable-leak patch through
`make_app.sh` `KK_PREFIX`.

Drawable leak, refcount checked in mesa-26.2.5 (Homebrew's version):
- +1 `wsi_metal_layer_acquire_drawable` (wsi_common_metal_layer.m:274, `[drawable retain]`)
- +1 `kk_encode_drawable_present` (kk_wsi.c:74, once per blit command buffer; one queue)
- -1 the WSI drops its own reference after encoding (wsi_common_metal.c:474)
- the command buffer's reference is never dropped: kk_queue.c:96 only stores NULL.
  `drawable` is outside `cmd->state`, so reset/re-record (kk_cmd_buffer.c:195) does not touch it.
Patch: `tools/package/patches/kosmickrisp-drawable-release.patch` adds
`mtl_release(cmd_buffer->drawable);` after `mtl_drawable_present` (no other release exists).

Build (in place, apply the patch with `patch -p1` in the source root first):
```sh
cd <work>/hp-mesa   # src/mesa-26.2.5 from mesa-26.2.5.tar.xz
python3 -m venv venv && venv/bin/pip install mako pyyaml packaging
printf "[binaries]\nllvm-config = '/opt/homebrew/opt/llvm/bin/llvm-config'\n" > native.ini
export PATH=$PWD/venv/bin:/opt/homebrew/opt/bison/bin:$PATH \
  PKG_CONFIG_PATH=/opt/homebrew/opt/spirv-llvm-translator/lib/pkgconfig:/opt/homebrew/opt/spirv-tools/lib/pkgconfig:/opt/homebrew/lib/pkgconfig
meson setup build-kk src/mesa-26.2.5 --native-file native.ini --prefix=<work>/hp-mesa-install \
  --buildtype=release -Db_ndebug=true \
  -Dcpp_args="-isystem /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1" \
  -Dvulkan-drivers=kosmickrisp -Dgallium-drivers= -Dllvm=enabled -Dopengl=false -Dgles1=disabled \
  -Dgles2=disabled -Degl=disabled -Dglx=disabled -Dgbm=disabled -Dplatforms=macos -Dvideo-codecs= \
  -Dvulkan-layers= -Dtools= -Dbuild-tests=false
ninja -C build-kk && ninja -C build-kk install
```
The `-isystem` flag is needed because the Command Line Tools clang++ here does not find
`<cstdint>` (its toolchain libc++ dir lacks it); without it the SPIRV-Tools check in
src/compiler/clc/meson.build:98 fails. The library links only zstd and SPIRV-Tools (no X11).
App: `make_app.sh` takes `KK_PREFIX=<work>/hp-mesa-install`.

Runs at 2056x1290 (hp-pkg-build/lhp_boot, 4533590, -O2), job tmp `kk/`:

| Run | Setting | Phase | Frames | Median | p99 | >16.7 | >33.3 |
|---|---|---|---|---|---|---|---|
| fd1 | default | cutscene 45-235 | 20,875 | 8.33 | 16.91 | 256 | 33 |
| fd1 | default | gameplay 235-336 | 10,993 | 8.33 | 17.75 | 319 | 27 |
| fl2a | `d3d9.maxFrameLatency = 2` | cutscene | 22,283 | 8.33 | 17.00 | 354 | 5 |
| fl2a | `d3d9.maxFrameLatency = 2` | gameplay | 11,528 | 8.34 | 17.54 | 485 | 3 |
| fd2 | default | cutscene | 22,460 | 8.33 | 16.95 | 296 | 1 |
| fd2 | default | gameplay | 11,310 | 8.34 | 17.54 | 551 | 2 |
| fl2b | `d3d9.maxFrameLatency = 2` | cutscene | 22,053 | 8.33 | 17.08 | 422 | 6 |
| fl2b | `d3d9.maxFrameLatency = 2` | gameplay | 11,610 | 8.33 | 17.49 | 417 | 1 |
| fix | patched ICD + latency 2 | cutscene | 22,291 | 8.34 | 16.72 | 227 | 3 |
| fix | patched ICD + latency 2 | gameplay | 11,549 | 8.34 | 17.55 | 470 | 2 |

Latency 2 against the default: medians are equal. Over 16.7 ms in the cutscene, the
default gave 256 and 296 and latency 2 gave 354 and 422. In gameplay, the default gave 319
and 551 and latency 2 gave 485 and 417. So latency 2 is not better and is likely worse in
the cutscene. Only fd1 (load average 40, other jobs on the machine) had more than 6 frames over 33 ms.

Leak, live CAMetalDrawable from `heap`: unpatched (fd1) 4,074 at 40 s and 8,354 at 85 s;
patched (fix) 1 at 40 s and 1 at 85 s. The patch removes the leak.

Test app (`make_app.sh` with `KK_PREFIX`, <work>/hp-app-build): `codesign --verify
--deep --strict` passes; no /opt/homebrew or /Users in any load command or rpath of the 10
Mach-Os (llvm-objdump). A 60 s launch with a private HOME loaded "KosmicKrisp 26.2.5" from
the bundle at 2056x1290 FIFO. The screenshot is not valid: another lane's Hobbit run started
during the launch and covered the window (log: "D3D9 window hidden at Present #2922"). Its
busy check looks for `lhp_boot` and `HarryPotterRecompile[d]`, and the app process is `lhp`.

## Second leak: render-pass colorAttachments array (2026-10-24)

`heap` in run val3 (stock 26.2.5, 8 min): MTLRenderPassColorAttachmentDescriptorArrayInternal
(96 B) went from 271,077 to 1,217,438, about 3,000 per second (one per render pass). No
render-pass descriptor class grew, so the descriptor itself was freed and only its array leaked.
Cause: src/kosmickrisp/bridge/mtl_render_state.m:25 returns `[[MTL4RenderPassDescriptor new]
init]`. `+new` already runs `-init`; the second `-init` builds a new colorAttachments array and
drops the first one without a release. The bridge is not ARC (bridge/meson.build:61 has no
`-fobjc-arc`); kk_cmd_draw.c:291 creates one descriptor per vkCmdBeginRendering and
kk_cmd_draw.c:561 releases it, so that balance is correct.
Proof outside the game: 10,000 `[[MTL4RenderPassDescriptor new] init]` + release left 10,000
arrays; `[MTL4RenderPassDescriptor new]` + release left 0. The other `[[X new] init]` sites
(MTL4ArgumentTableDescriptor, MTLStencilDescriptor, MTLDepthStencilDescriptor,
MTL4CommitOptions) leave nothing, so they are not changed.
Patch: `tools/package/patches/kosmickrisp-renderpass-release.patch` (apply with `patch -p1`
after the drawable patch). Test build: build dir `build-kk2`, prefix `<work>/hp-mesa-install2`.

| Run | Driver | Color arrays early -> late | CAMetalDrawable early -> late | Footprint |
|---|---|---|---|---|
| val3 | stock 26.2.5 | 271,077 -> 1,217,438 | 12,652 -> 21,374 | 2662 -> 3174 MB |
| leak2 | both patches | 0 -> 0 | 2 -> 1 | 2458 -> 2662 MB |

`tools/check/reliability.sh` cannot select a driver: for a bare binary, `BARE_ENV`
(tools/check/reliability.py:59) is merged after `os.environ` (reliability.py:116), so a
`VK_DRIVER_FILES` from the environment is overwritten. leak2 used a wrapper that set
`BARE_ENV["VK_DRIVER_FILES"]` before calling `main`.
