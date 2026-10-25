# DXVK Native D3D9 on this Mac: triangle and textured-quad test

Date: 2026-10-24. Machine: Apple M1 Max, macOS 26.6.2 (25G83), arm64.

**Verdict: DXVK Native D3D9 WORKS on KosmicKrisp. DOES NOT WORK on MoltenVK: first failing step is `vkCreateInstance` ("Found drivers that contain devices which support the portability subset, but the instance does not enumerate portability drivers!"); with that gate bypassed, DXVK rejects the device: "Skipping: Device does not support required feature 'shaderCullDistance'" (MoltenVK also lacks `robustBufferAccess2` and `nullDescriptor`).**

Upstream DXVK needs all four `jm2/dxvk` patches. Patches 1-2 are needed to build and load. Patches 3-4 are needed for DXVK to accept KosmicKrisp as an adapter.

**The SDL2 window backend also works on KosmicKrisp (2026-10-24 01:07).** The same checks pass with the same pixel values, using DXVK built with `-Dnative_sdl2=enabled -Dnative_sdl3=disabled`, the test window created through SDL2, and `DXVK_WSI_DRIVER=SDL2`. That build is installed in `~/Developer/dxvk-install`. See "Linking the game".

## Results

| Check | KosmicKrisp 26.2.5 | MoltenVK 1.4.2 |
|---|---|---|
| Device creation | PASS | FAIL (no instance; then no adapter) |
| Clear | PASS | not reached |
| Vertex-coloured triangle | PASS (9/9 points) | not reached |
| Textured quad, 4x4 texels | PASS (16/16 exact) | not reached |
| 300 presents | 2524.6 ms, median 8.105 ms | not reached |

### KosmicKrisp, SDL2 window backend (current build, 01:07)

```
adapter: AMD Radeon RX 6700 XT / aticfx32.dll
caps: VS 3.0 PS 3.0
PASS clear: expected 0x204060 at all 65536 pixels; (0,0)=0x204060 (128,128)=0x204060 (255,255)=0x204060; mismatches=0 first=0x000000
PASS triangle(128,162): got 0x555555 expected ~555555 (tol 4)
PASS triangle(40,222): got 0xe80d0a expected ~e80d0a (tol 4)
PASS triangle(216,222): got 0x0de80a expected ~0de80a (tol 4)
PASS triangle(128,40): got 0x0909ed expected ~0909ed (tol 4)
PASS triangle(100,180): got 0x833d3f expected ~833d3f (tol 4)
PASS triangle(160,120): got 0x136389 expected ~136389 (tol 4)
PASS triangle-outside(10,10): got 0x204060 expected 0x204060
PASS triangle-outside(245,10): got 0x204060 expected 0x204060
PASS triangle-outside(128,250): got 0x204060 expected 0x204060
PASS textured-quad: 16/16 texel centres exact; got/expected (0,0)=0x2828c8/0x2828c8 ... (3,3)=0xdcdcc8/0xdcdcc8 (same 16 values as below)
timing: 300 presents in 2556.3 ms; mean 8.521 ms; median 8.031 ms; min 3.422 ms; max 47.585 ms (interval IMMEDIATE)
RESULT PASS (0 failed checks)
```

The binary links `/opt/homebrew/opt/sdl2-compat/lib/libSDL2-2.0.0.dylib`. The installed `libdxvk_d3d9` contains `Sdl2WsiDriver` and no SDL3 window-backend symbols. The MoltenVK results with SDL2 match the SDL3 results below; both fail before any window code runs.

Homebrew's `libSDL2` is **sdl2-compat 2.32.74**: the SDL2 API implemented on top of SDL3 3.4.18. The game's "SDL2" is therefore SDL3 underneath. Both `sdl2` 2.32.10 and `sdl2-compat` are in the Cellar, but `/opt/homebrew/lib/libSDL2*.dylib` link to sdl2-compat.

### KosmicKrisp, SDL3 window backend (first build, verbatim, DXVK `info:` lines removed)

```
adapter: AMD Radeon RX 6700 XT / aticfx32.dll
warn:  D3D9DeviceEx::SetupFPU: not supported on this arch.
caps: VS 3.0 PS 3.0
PASS clear: expected 0x204060 at all 65536 pixels; (0,0)=0x204060 (128,128)=0x204060 (255,255)=0x204060; mismatches=0 first=0x000000
PASS triangle(128,162): got 0x555555 expected ~555555 (tol 4)
PASS triangle(40,222): got 0xe80d0a expected ~e80d0a (tol 4)
PASS triangle(216,222): got 0x0de80a expected ~0de80a (tol 4)
PASS triangle(128,40): got 0x0909ed expected ~0909ed (tol 4)
PASS triangle(100,180): got 0x833d3f expected ~833d3f (tol 4)
PASS triangle(160,120): got 0x136389 expected ~136389 (tol 4)
PASS triangle-outside(10,10): got 0x204060 expected 0x204060
PASS triangle-outside(245,10): got 0x204060 expected 0x204060
PASS triangle-outside(128,250): got 0x204060 expected 0x204060
PASS textured-quad: 16/16 texel centres exact; got/expected (0,0)=0x2828c8/0x2828c8 (1,0)=0x6428c8/0x6428c8 (2,0)=0xa028c8/0xa028c8 (3,0)=0xdc28c8/0xdc28c8 (0,1)=0x2864c8/0x2864c8 (1,1)=0x6464c8/0x6464c8 (2,1)=0xa064c8/0xa064c8 (3,1)=0xdc64c8/0xdc64c8 (0,2)=0x28a0c8/0x28a0c8 (1,2)=0x64a0c8/0x64a0c8 (2,2)=0xa0a0c8/0xa0a0c8 (3,2)=0xdca0c8/0xdca0c8 (0,3)=0x28dcc8/0x28dcc8 (1,3)=0x64dcc8/0x64dcc8 (2,3)=0xa0dcc8/0xa0dcc8 (3,3)=0xdcdcc8/0xdcdcc8
timing: 300 presents in 2524.6 ms; mean 8.415 ms; median 8.105 ms; min 2.653 ms; max 34.127 ms (interval IMMEDIATE)
RESULT PASS (0 failed checks)
```

The run was repeated twice with the same pixel values (first run: 300 presents in 2516.7 ms, median 8.002 ms).

Notes on the KosmicKrisp run:
- Triangle expected values are computed in the test by barycentric interpolation of the three vertex colours at the pixel. The read-back values match exactly, so the tolerance was not used.
- The textured quad uses point sampling. Every texel came back bit-exact, so the KosmicKrisp texture path does not show the "textures sample black" fault that the kisakcod notes report for MoltenVK.
- The median present time of about 8 ms with `D3DPRESENT_INTERVAL_IMMEDIATE` matches a 120 Hz display. DXVK did get `VK_PRESENT_MODE_IMMEDIATE_KHR`, so the pacing likely comes from the Metal layer under KosmicKrisp. This is an inference; I did not measure it further.
- DXVK hides the Apple vendor ID and reports "AMD Radeon RX 6700 XT / aticfx32.dll" through `GetAdapterIdentifier`. The game will see that identity unless it is overridden in `dxvk.conf`.
- DXVK logs "Binding model: Legacy" and "Graphics pipeline libraries not supported" on KosmicKrisp. Shaders therefore compile at first draw, not at load.

### MoltenVK output

Stock manifest `/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json`:

```
[Vulkan Loader] ERROR | DRIVER: vkCreateInstance: Found drivers that contain devices which support the portability subset, but the instance does not enumerate portability drivers! Applications that wish to enumerate portability drivers must set the VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR bit in the VkInstanceCreateInfo flags and enable the VK_KHR_portability_enumeration instance extension.
[Vulkan Loader] ERROR | DRIVER: vkCreateInstance: Found no drivers!
err:   DxvkInstance::createInstance: Failed to create Vulkan instance
libc++abi: terminating due to uncaught exception of type dxvk::DxvkError
```

Cause: DXVK does not set the portability-enumeration flag, and MoltenVK is marked `is_portability_driver`. To get past this without patching DXVK, I used a copy of the manifest with `"is_portability_driver": false` (`~/Developer/hp-dxvk/icd/MoltenVK_icd_noportability.json`). Result:

```
info:  Found device: Apple M1 Max (MoltenVK 0.2.2210)
info:    Skipping: Device does not support required feature 'shaderCullDistance'
warn:  DXVK: No adapters found. Please check your device filter settings
warn:  and Vulkan drivers. A Vulkan 1.3 capable setup is required.
libc++abi: terminating due to uncaught exception of type dxvk::DxvkError
```

I compared DXVK's required-feature list (`src/dxvk/dxvk_device_info.cpp`, 51 entries with `true` or `DXVK_MACOS_REQUIRED`) with `vulkaninfo` output for each driver:

| Required feature | KosmicKrisp | MoltenVK 1.4.2 |
|---|---|---|
| `fillModeNonSolid` | false (made optional by patch 4) | true |
| `geometryShader` | false (made optional by patch 4) | false (made optional by patch 4) |
| `VK_EXT_depth_clip_enable.depthClipEnable` | absent (made optional by patch 4) | absent (made optional by patch 4) |
| `shaderCullDistance` | true | **false** |
| `robustness2.robustBufferAccess2` | true | **false** |
| `robustness2.nullDescriptor` | true | **false** |

`nullDescriptor` is part of DXVK's descriptor binding model: unbound slots are written as null descriptors. This confirms the anecdote that DXVK on MoltenVK fails on a binding-model feature. Relaxing it would need DXVK descriptor-code changes, not a small portability fix, so I stopped there. The kisakcod notes say the same: their `kisakcod-moltenvk` branch added "null-descriptor substitutes", and texture sampling still read zero.

## What I installed and from where

| Item | Version | Source |
|---|---|---|
| Mesa with KosmicKrisp | 26.2.5, bottle `mesa--26.2.5.arm64_tahoe` | `brew install mesa` (homebrew-core; the formula builds `-Dvulkan-drivers=kosmickrisp,swrast` on macOS 26) |
| KosmicKrisp ICD | apiVersion 1.4.354, driverVersion 26.2.5, conformanceVersion 1.4.3.2 | `/opt/homebrew/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json` |
| MoltenVK | 1.4.2 (already installed), apiVersion 1.4.357 | `/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json` |
| Vulkan loader | 1.4.363 (already installed) | `/opt/homebrew/lib/libvulkan.1.dylib` |
| SDL3 | 3.4.18 (already installed) | Homebrew |
| glslang | 16.6.0 (already installed) | Homebrew |
| meson, ninja (arm64) | 1.12.1, 1.13.2 | `brew install meson ninja` |
| DXVK | master `02e2028c` (v3.1.1-153, 2026-10-21) | https://github.com/doitsujin/dxvk |

I did not use the LunarG Vulkan SDK. The Homebrew mesa bottle gave KosmicKrisp in one non-interactive step.

## Homebrew side effects

`brew install mesa` (2026-10-24, about 00:40) changed shared Homebrew packages:

| Package | Before | After | Effect |
|---|---|---|---|
| `llvm` | 21.1.3_1 | 23.1.4 | `/opt/homebrew/opt/llvm` now points to 23.1.4. 21.1.3_1 is still in `/opt/homebrew/Cellar/llvm/`. Any build that uses Homebrew `clang`, `lld` or `libLLVM` now gets LLVM 23. |
| `z3` | 4.15.3 | 5.3.1 | Major version change. Only matters to tools that link z3. |
| new: `mesa` 26.2.5, `spirv-llvm-translator` 23.1.2, `xcb-util-keysyms` 0.4.1 | | | KosmicKrisp and lavapipe ICDs appear in `/opt/homebrew/share/vulkan/icd.d/`. The Vulkan loader now finds KosmicKrisp and lavapipe as well as MoltenVK when `VK_DRIVER_FILES` is not set. |

`brew install meson ninja` added arm64 `meson` 1.12.1 and `ninja` 1.13.2 in `/opt/homebrew/bin`. The older x86_64 `/usr/local/bin/ninja` and the miniforge `meson` still come first in the default `PATH`.

Apple's system compiler (`/usr/bin/clang`, Apple clang 21) is not affected. DXVK and the test were built with it.

## Toolchain problems on this machine (not DXVK problems)

1. `/usr/local/bin/ninja` is an x86_64 binary. Compilers it starts run under Rosetta and `xcrun` fails ("libxcrun.dylib ... need 'x86_64'"). Fix: use `/opt/homebrew/bin/ninja` and `/opt/homebrew/bin/meson`.
2. Command Line Tools 27.0 ship `/Library/Developer/CommandLineTools/usr/include/c++/v1` with only one file, `__cxx_version`. Apple clang searches that directory first and does not fall back to the SDK, so `#include <array>` fails. Fix without changing the system: `CXXFLAGS="-nostdinc++ -isystem $(xcrun --show-sdk-path)/usr/include/c++/v1"`.

## Build commands

All of these are in `tools/dxvk_test/build.sh` (`build.sh all` builds DXVK, builds the test, and runs it on the three manifests).

```
export PATH=/opt/homebrew/bin:$PATH
cd ~/Developer/hp-dxvk
git clone --recursive https://github.com/doitsujin/dxvk.git
cd dxvk
git remote add jm2 https://github.com/jm2/dxvk.git && git fetch jm2 kisakcod-macos
git checkout -b hp-macos origin/master
git cherry-pick cf2fb08b e475a8d6 99ee0e5c a598c71d
SDK=$(xcrun --show-sdk-path)
CXXFLAGS="-nostdinc++ -isystem $SDK/usr/include/c++/v1" meson setup ../build-sdl2 --buildtype=release \
  -Denable_dxgi=false -Denable_d3d8=false -Denable_d3d10=false -Denable_d3d11=false -Denable_d3d9=true \
  -Dnative_sdl2=enabled -Dnative_sdl3=disabled -Dnative_glfw=disabled -Dnative_macos=true \
  --prefix=~/Developer/dxvk-install
/usr/bin/lockf -k <lock> ninja -C ../build-sdl2 -j6
meson install -C ../build-sdl2
```

The four patches do not change any window-backend code under `src/wsi`. Patch 1 changes `src/util/util_win32_compat.h`, which both SDL backends use for `LoadLibraryA`/`GetProcAddress`.

The first, SDL3 build used the same commands with `-Dnative_sdl3=enabled -Dnative_sdl2=disabled`, build directory `../build-up`, and `meson install --destdir ../stage`. Its output is in `~/Developer/hp-dxvk/stage/opt/homebrew/lib/`.

Run: `VK_DRIVER_FILES=<manifest> DXVK_WSI_DRIVER=SDL2 DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib ~/Developer/hp-dxvk/test/d3d9_triangle`

## Linking the game

Install prefix: `~/Developer/dxvk-install` (DXVK master `02e2028c` + the 4 patches, d3d9 only, SDL2 window backend, `-Dnative_macos=true`, release build).

| Path | Content |
|---|---|
| `lib/libdxvk_d3d9.0.dylib` | The library. Install name `@rpath/libdxvk_d3d9.0.dylib`. Links only `libSystem` and `libc++`. |
| `lib/libdxvk_d3d9.dylib` | Symlink for `-ldxvk_d3d9`. |
| `lib/pkgconfig/dxvk-d3d9.pc` | `Cflags: -I${includedir}/dxvk`, `Libs: -L${libdir} -ldxvk_d3d9` |
| `include/dxvk/` | 78 headers: every file of DXVK's `include/native/windows` (`windows.h`, `windows_base.h`, `unknwn.h`) and `include/native/directx` (`d3d9.h`, `d3d9types.h`, `d3d9caps.h`, ...), plus `include/native/wsi`. `meson install` puts them in one flat folder. This is the layout DXVK's own pkg-config file uses, so I did not add a second copy in `include/native/...`. |

### Compile and link a C file

```
DXVK=~/Developer/dxvk-install
clang -std=c11 -I$DXVK/include/dxvk -I/opt/homebrew/include/SDL2 -c game.c -o game.o
clang game.o -o game -L$DXVK/lib -ldxvk_d3d9 -Wl,-rpath,$DXVK/lib -L/opt/homebrew/lib -lSDL2
```

A C file must include the headers like this:

```c
#define COBJMACROS                 /* optional: IDirect3D9_CreateDevice(p, ...) style macros */
#include <windows.h>
#undef THIS_
#undef THIS
#define THIS_ INTERFACE *This,
#define THIS INTERFACE *This
#include <d3d9.h>
```

Reason: DXVK Native's `windows_base.h` defines `THIS_` and `THIS` as empty for C as well as C++. In C the vtable function-pointer types would then have no `This` argument, and `IDirect3D9_GetAdapterIdentifier(d3d, 0, 0, &id)` fails to compile: "too many arguments to function call, expected 3, have 4". `d3d9.h` defines `INTERFACE` before each of its 20 interfaces, so the MinGW C definitions above work. The binary layout is COM-compatible: DXVK's C++ `IUnknown` has no virtual destructor, so the vtable is `QueryInterface, AddRef, Release`, then the methods in declaration order, with `this` as the first argument.

`tools/dxvk_test/link_probe.c` follows this recipe. It calls `Direct3DCreate9`, `GetAdapterCount`, `GetAdapterIdentifier` and `Release` through the C macros. Output on KosmicKrisp:

```
adapters: 1
adapter 0: AMD Radeon RX 6700 XT
PASS link_probe
```

The libc++ header workaround is **not** needed for C files. The game needs it for any C++ file it compiles with Apple clang, and DXVK needs it to rebuild:
`-nostdinc++ -isystem $(xcrun --show-sdk-path)/usr/include/c++/v1`

A C program linked with `clang` (not `clang++`) is fine. `libdxvk_d3d9` brings in `libc++` itself.

### Order of calls

1. `SDL_Init(SDL_INIT_VIDEO)` **before** `Direct3DCreate9`. DXVK's SDL2 backend does not start SDL video itself; the SDL3 backend does. Without it, `Direct3DCreate9` crashes with `EXC_BAD_ACCESS` (address 0x208) in `SDL_Vulkan_GetInstanceExtensions` (sdl2-compat calling SDL3). Stack: `Direct3DCreate9` -> `DxvkInstance::initVulkanInstance` -> `Sdl2WsiDriver::getInstanceExtensions` -> `libSDL2 SDL_Vulkan_GetInstanceExtensions` -> `libSDL3 SDL_Vulkan_GetInstanceExtensions_REAL`. This is a crash, not a C++ exception, so a terminate handler does not catch it.
2. Create the window with `SDL_WINDOW_VULKAN` and pass the `SDL_Window*` cast to `HWND` as `hFocusWindow` and `D3DPRESENT_PARAMETERS.hDeviceWindow`.
3. DXVK reports fatal errors as C++ exceptions (`dxvk::DxvkError`) that can leave `Direct3DCreate9`. A C caller cannot catch them. The game should install a `std::set_terminate` handler in a C++ file, or call D3D9 through a small C++ shim with `try`/`catch`, so a failure exits cleanly instead of calling `abort()`.

### Run-time environment

| Variable | Value | Required? |
|---|---|---|
| `VK_DRIVER_FILES` | `/opt/homebrew/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json` | Recommended. Without it the loader also offers lavapipe (a CPU driver). It does not offer MoltenVK, because DXVK does not enumerate portability drivers. I did not test a run without it. The path is a Homebrew symlink and follows `brew upgrade mesa`. |
| `DXVK_WSI_DRIVER` | `SDL2` | Set it. This build has only the SDL2 backend. |
| `DYLD_FALLBACK_LIBRARY_PATH` | `/opt/homebrew/lib` | Yes. DXVK opens `libSDL2-2.0.0.dylib` and `libvulkan.1.dylib` with `dlopen` by file name only. Without the variable, `Direct3DCreate9` throws in `Sdl2WsiDriver::Sdl2WsiDriver()` ("Failed to load SDL2 DLL"), even though the program already links libSDL2 by its full path. Confirmed under lldb with a breakpoint on `__cxa_throw`. |
| `DXVK_LOG_LEVEL`, `DXVK_LOG_PATH` | e.g. `warn`, a folder | Optional. Logs to stderr by default. |
| `DXVK_STATE_CACHE_PATH` | a folder | Optional. The default pipeline cache is `~/.cache/dxvk/`. |

## Patches applied

All four come from https://github.com/jm2/dxvk, branch `kisakcod-macos` (head `52ae487b`; the code commits are below it). Each applied to upstream master `02e2028c` without conflict. Copies are in `tools/dxvk_test/patches/`.

| # | Fork commit | What it does | Needed because | Upstream status |
|---|---|---|---|---|
| 1 | `cf2fb08b` [native] Fix building DXVK Native on macOS | `__APPLE__` for the `LoadLibraryA`/`GetProcAddress` shims; `<cstddef>` in `util_math.h`; skip `--version-script` on darwin; `bit::tzcnt` overload by width | Upstream build fails: `wsi_platform_sdl3.cpp:13:14: error: use of undeclared identifier 'LoadLibraryA'` | Same change as open PR https://github.com/doitsujin/dxvk/pull/5962 (jm2, opened 2026-10-09, branch `native-macos-build`) |
| 2 | `e475a8d6` [native] Find the executable and the Vulkan loader on macOS | `_NSGetExecutablePath` in `getExePath`; `libvulkan.1.dylib` / `libvulkan.dylib` loader names | Without it DXVK cannot find the macOS Vulkan loader, and `getExePath` has no return on macOS. I applied it before the first run, so I did not observe the failure without it | Fork only |
| 3 | `99ee0e5c` [dxvk] Clip depth through depth clamp when VK_EXT_depth_clip_enable is absent | Chains the depth-clip struct only when the extension exists; otherwise `depthClampEnable = !depthClip` | Required by patch 4: without it DXVK chains an unsupported struct on KosmicKrisp | Fork only |
| 4 | `a598c71d` [kisakcod] Add -Dnative_macos | Makes `geometryShader`, `fillModeNonSolid`, `depthClipEnable` optional when built with `-Dnative_macos=true` | With patches 1-2 only, on KosmicKrisp: `Skipping: Device does not support required feature 'fillModeNonSolid'` then `No adapters found` | Fork only |

I wrote no DXVK patches of my own.

## Test program

`tools/dxvk_test/d3d9_triangle.cpp` (about 300 lines). It:
- creates a 256x256 SDL2 window (`SDL_WINDOW_VULKAN`), after `SDL_Init(SDL_INIT_VIDEO)`, and passes the `SDL_Window*` as `HWND`. The first version used SDL3; the SDL3 results above come from that version;
- calls `Direct3DCreate9` and `CreateDevice` (HAL, hardware VP, windowed, X8R8G8B8 back buffer, `D3DPRESENT_INTERVAL_IMMEDIATE`);
- uses hand-assembled `vs_2_0`/`ps_2_0` token streams: a colour pair (`mov oPos, v0; mov oD0, v1` / `mov oC0, v0`) and a texture pair (`mov oT0, v1` / `texld r0, t0, s0`);
- draws the triangle from a `D3DPOOL_DEFAULT` vertex buffer, and the quad with `DrawPrimitiveUP` and a `D3DPOOL_MANAGED` 4x4 A8R8G8B8 texture with point sampling;
- reads back with `GetBackBuffer` + `GetRenderTargetData` into a `D3DPOOL_SYSTEMMEM` surface + `LockRect`, before `Present`;
- installs a `std::set_terminate` handler and wraps `Direct3DCreate9` and `CreateDevice` in `try`/`catch`. DXVK reports fatal errors as C++ exceptions (`dxvk::DxvkError`). The first six failed runs (00:52-00:54) called `abort()`, which made macOS crash reports and "quit unexpectedly" dialogs. With the handler, a failed run prints `FAIL ... threw` and exits with code 2. It leaves no crash report (checked in `~/Library/Logs/DiagnosticReports` after the 00:57 run).

`build.sh run` sets `DXVK_LOG_LEVEL=debug`, `DXVK_LOG_PATH` and `VK_LOADER_DEBUG=error`. It writes one log folder per driver manifest under `<job tmp>/dxvk-logs/`. The 00:57 run gave the same results as the earlier runs: KosmicKrisp PASS with the same pixel values (300 presents in 2518.3 ms, median 8.197 ms), and both MoltenVK cases fail with the errors above, now with exit code 2.

## What this test does not cover

- No Khronos validation layer run (not installed here).
- Depth: no depth buffer, so patch 3's depth-clamp fallback is not exercised.
- No fixed-function pipeline, no `ProcessVertices`, no DXT formats, no render-target textures, no `.fx` or D3DX.

## Leads from `d3d9_host_options.md`

| Claim | Result |
|---|---|
| Four patches on `jm2/dxvk` branch `kisakcod-macos` make DXVK Native D3D9 build and run on M1 Max | **Confirmed.** The branch exists with exactly these four code commits. All four were needed here, and the result passes. The pinned commit `dac6ce62` exists (it is the documentation commit above `a598c71d`) |
| Patch 1 is upstream PR #5962, open, no review | **Confirmed** open, author jm2, created 2026-10-09 |
| `jm2/kisakcod` PR #589 pins the fork for KosmicKrisp | **Confirmed**: "build: KISAK_DXVK_MACOS builds the pinned jm2/dxvk fork for KosmicKrisp", merged 2026-10-09 |
| KosmicKrisp lacks only `geometryShader`, `fillModeNonSolid`, `depthClipEnable` | **Confirmed** against DXVK master `02e2028c` and KosmicKrisp 26.2.5 |
| KosmicKrisp installs with `brew install mesa` | **Confirmed** on macOS 26 |
| DXVK on MoltenVK "every texture sampled black" | **Not reproduced and not reachable.** Current upstream DXVK will not create a device on MoltenVK 1.4.2: three required features are missing. The kisakcod notes say they needed portability-enumeration and null-descriptor patches to get that far |
| nfsu2-recompiled macOS port runs DXVK Native + MoltenVK at 60 FPS | **The port exists, but it uses a different DXVK.** Issue https://github.com/codepdbh/nfsu2-recompiled/issues/901 (2026-10-18) links https://github.com/daniiichan/Need-for-Speed-Underground-2-macOS. Its vendored DXVK is `native-1.9.2b-android-arm64` (pinned `c8dc91fa`), a customised 2021-era DXVK with per-API feature requirements, `VK_KHR_portability_subset` enabled under `__APPLE__`, and a DXSO shadow-sampler fix. It does not show that current upstream DXVK works on MoltenVK. I did not build or run it |

## Files

- Test source: `tools/dxvk_test/d3d9_triangle.cpp` (SDL2)
- C link probe: `tools/dxvk_test/link_probe.c`
- Build and run script: `tools/dxvk_test/build.sh` (SDL2 build, installs to `~/Developer/dxvk-install`)
- Durable install: `~/Developer/dxvk-install/{lib,include/dxvk}`
- SDL2 build tree: `~/Developer/hp-dxvk/build-sdl2`; run logs for each driver: `<job tmp>/dxvk-logs/` (job scratch; not durable)
- Patch copies: `tools/dxvk_test/patches/0001..0004-*.patch`
- DXVK source (branch `hp-macos`), build tree, stage, test binary: `~/Developer/hp-dxvk/{dxvk,build-up,stage,test}`
- Raw logs: `~/Developer/hp-dxvk/{kk-run1,kk-run2,mvk-run1,mvk-run2,run-all}.log`; vulkaninfo dumps: `~/Developer/hp-dxvk/icd/vulkaninfo-{kk,mvk}.txt`
