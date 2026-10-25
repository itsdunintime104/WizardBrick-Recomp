# Host Direct3D 9 for a native arm64 macOS static recompilation

Decision document. Research date: 2026-10-24. Read-only; no code written.

Target: LEGO Harry Potter Years 1-4 (`lhp_win32.exe`, PE32 x86-32, MSVC 2005, TT Games "nu2").
Host: Apple M1 Max, macOS 26, native arm64. No Wine, no Rosetta at run time.

Evidence labels used below: **[V]** verified from a primary source I read; **[I]** inference I drew from verified facts.

---

## 1. Recommendation

**Link DXVK Native, D3D9 only, built from upstream `doitsujin/dxvk` plus the four `jm2/dxvk` `kisakcod-macos` patches. Run it over KosmicKrisp (Mesa's Vulkan-on-Metal driver), with MoltenVK as the fallback driver.**

**For the D3DX9 side: write our own thin `d3dx9` (13 entry points) that delegates all HLSL and `.fx` compilation to `libvkd3d-shader`, and port Wine's `effect.c` effect runtime and `shader.c` constant-table parser as the two pieces that are expensive to rewrite.**

Fallback if DXVK's D3D9 proves unworkable: write a D3D9 subset directly on Metal and get shaders from `libvkd3d-shader`'s MSL target (HLSL or legacy D3D bytecode in, Metal Shading Language out). This keeps the shader compiler decision unchanged, so it is a backend swap, not a restart.

### Ranked comparison

| Option | Licence | macOS arm64 status | Work needed | Risk |
|---|---|---|---|---|
| **DXVK Native, d3d9 only** (upstream + 4 patches) | zlib/libpng **[V]** | **Runs.** Device creation, draws and Present verified on M1 Max, macOS 27, 2026-10-09, over KosmicKrisp **[V]** | Rebase 4 patches; set `DXVK_WSI_DRIVER=SDL3`; build with `-Dnative_macos=true`; supply the COM shim | **Low-med** |
| **DXVK Native over MoltenVK** | zlib/libpng **[V]** | Creates a device and draws, but **every texture sampled black** on MoltenVK **[V]**. A third-party NFSU2 macOS port claims 60 FPS on this stack **[V]** | Same as above, plus diagnosing the texture bug | **Medium** |
| **D3D9 subset on Metal + vkd3d-shader MSL** | our code + LGPL-2.1 shader lib **[V]** | N/A, does not exist | Very large: write the state machine, fixed-function emulation, all resource paths | **High** |
| **DXVK-macOS forks** (Gcenx, metalsharp) | zlib **[V]** | Wrong shape: both produce **Windows PE DLLs for Wine**, x86_64/i386 **[V]** | Would mean redoing them as a native build, with no gain over upstream + patches | **High** |
| **WineD3D standalone** | LGPL-2.1 | No native macOS build known. The Windows builds need an `opengl32`/`ddraw` PE chain **[V]** | Strip PE/Win32 deps; supply a GL context. macOS caps at OpenGL 4.1 and Apple deprecated it | **High** |
| **D3DMetal** (Apple GPTK) | Proprietary; terms reported to forbid use in a shipping title **[V]** | D3D11/12 only; no D3D9 **[V]** | N/A | **Blocked** |
| **DXMT** | - | D3D10/11 only; no D3D9 in its stated scope **[V]** | N/A | **No D3D9** |
| **gl4es** | MIT | Not a D3D9 implementation at all. It maps desktop OpenGL 2.1/1.5 to GL ES 2.0/1.1 **[V]** | N/A | **Not applicable** |
| **Gallium Nine** | - | A D3D9 frontend for **Mesa on Linux**; needs a Gallium driver **[V]** | No Gallium path on macOS | **Not applicable** |

---

## 2. Question 1 - which D3D9 implementation can be linked natively

### 2.1 DXVK Native is the only live candidate

Upstream `doitsujin/dxvk` carries DXVK Native in-tree; the separate `dxvk-native` repository is gone (404) and an AUR packager note from November 2022 says it was upstreamed **[V]**. Debian sid ships `libdxvk-native-dev` 2.7.1-3 and describes it as a Vulkan-based translation layer for Direct3D 8/9/10/11 that lets Direct3D applications originating on Windows be compiled natively **[V]**.

The upstream README confirms the parts that matter to us **[V]**:

- "DXVK Native is a version of DXVK which allows it to be used natively without Wine."
- It is intended for "game and application ports to either avoid having to write another rendering backend, or to help with port bringup during development."
- "DXVK Native comes with a slim set of Windows header definitions required for D3D9/11 and the MinGW headers for D3D9/11."
- A windowing backend must be chosen at run time: `DXVK_WSI_DRIVER` set to `SDL3`, `SDL2` or `GLFW`.

That last point answers the "how does it handle Windows types when built as a native library" question: DXVK Native ships its own minimal Windows typedefs and COM macros, and `HWND` becomes an `SDL_Window*` **[V]**. Our shim therefore gets a real C++ `IDirect3D9`/`IDirect3DDevice9` object with the standard vtable layout, which is exactly what the guest-memory trampoline plan needs **[I]**.

Licence: zlib/libpng, copyright Philip Rebohle **[V]**. No copyleft obligation and no attribution burden beyond the notice.

### 2.2 macOS arm64 is proven, but not by upstream

The strongest evidence is `jm2/kisakcod` PR #589, opened and merged 2026-10-09, tested on an **M1 Max running macOS 27** **[V]**. It builds DXVK through Meson/Ninja as a CMake `ExternalProject`, pinned to `jm2/dxvk` branch `kisakcod-macos`, commit `dac6ce62`, configured **D3D9 only, SDL3 WSI, `-Dnative_macos=true`** **[V]**.

The fork's `KISAK_MACOS.md` lists exactly four patches **[V]**:

1. **Build fixes** - `__APPLE__` shims for the `LoadLibraryA`/`GetProcAddress`/`FreeLibrary` helpers (Apple clang does not define `__unix__`), a `<cstddef>` include for `size_t`, skipping the GNU `--version-script` `.sym` files because Apple's ld64 rejects them, and disambiguating `bit::tzcnt` where `uintptr_t` and `uint64_t` differ in width. This is upstream PR **doitsujin/dxvk#5962**, still open, no maintainer review, no stated macOS support policy **[V]**.
2. **Executable and loader lookup** - `_NSGetExecutablePath` for `getExePath` (the original fell off the end of the function and trapped) and `libvulkan.1.dylib` loader names. Fork only **[V]**.
3. **Depth clip through depth clamp** - sets `depthClampEnable = !depthClip` and only chains the depth-clip struct when `VK_EXT_depth_clip_enable` exists. Without it, geometry past the far plane is clamped instead of clipped **[V]**.
4. **`-Dnative_macos`** - makes `geometryShader`, `fillModeNonSolid` and `depthClipEnable` optional. Fork only; **not** part of upstream PR #5962 **[V]**.

Those three features are the only ones KosmicKrisp lacks out of DXVK's 53 device requirements **[V]**. Softening them is safe for us for the reasons the fork gives: D3D9 uses a geometry shader only for `ProcessVertices` software-vertex-processing emulation, and `fillModeNonSolid` is wireframe, which is debug-only **[V]**. We must still confirm the nu2 engine never calls `ProcessVertices` **[I]**.

### 2.3 The Vulkan driver choice is the real decision

| Driver | Licence | macOS | Status with DXVK D3D9 |
|---|---|---|---|
| **KosmicKrisp** (LunarG, in Mesa) | MIT **[V]** | macOS 26+, Apple Silicon **[V]** | Device creation, draws and Present all succeeded; a green texture sampled `0x00ff00` and TFACTOR gave `0xff8000`; validation layer clean apart from DXVK's intentional `t2d`/`tcube` binding aliasing **[V]** |
| **MoltenVK** (Khronos) | Apache-2.0 | All Apple architectures including Apple Silicon **[V]** | DXVK could create a device and draw, but "every texture sampled black" **[V]**. A separate NFSU2 macOS port reports this stack working at 60 FPS **[V]** - the two reports conflict |

KosmicKrisp passed the Vulkan 1.4 CTS and is Khronos-conformant at 1.4; LunarG calls it production ready on modern macOS and shipped it in the Vulkan SDK on 29 September, targeting macOS 26+ **[V]**. It installs with `brew install mesa` and is selected at run time through `VK_DRIVER_FILES` **[V]**. Earlier conformance was Vulkan 1.3 on macOS 15+ and Vulkan 1.2 on macOS 13+ **[V]**.

Taking KosmicKrisp sets our minimum to **macOS 26**, which matches the host machine **[V]**.

### 2.4 What our shim must provide

From the facts above **[I]**:

- An SDL3 window, and `DXVK_WSI_DRIVER=SDL3` in the environment before `Direct3DCreate9`.
- A guest-side `IDirect3D9` object whose vtable slots trampoline into the host C++ object DXVK returns. One import to intercept: `Direct3DCreate9`.
- Translation of every `HWND` the guest passes (`D3DPRESENT_PARAMETERS.hDeviceWindow`, `CreateDevice`) into our `SDL_Window*`.
- Pointer-width marshalling. The guest is 32-bit; the host is 64-bit arm64. Every COM pointer, `D3DPRESENT_PARAMETERS`, `D3DLOCKED_RECT`, `D3DVERTEXELEMENT9` array and `D3DCAPS9` crossing the boundary needs a 32-bit shadow. This is the same problem the existing Hobbit and Marvel ports already solve for POSIX structs, but the D3D9 struct set is larger.
- Vulkan loader on the library path, plus `VK_DRIVER_FILES` pointing at KosmicKrisp.

### 2.5 Why the other options fail

- **Gcenx/DXVK-macOS** and the newer **metalsharp/DXVK-MacOS** (upstream DXVK v3.1, commit `70d7508c`, zlib) both **produce Windows PE DLLs** to drop into a Wine prefix; the Vulkan/MoltenVK dependency is loaded by Wine's Unix side **[V]**. They list x86_64 and i386 Windows targets, not arm64 **[V]**. They are Wine-prefix artefacts, which the owner has ruled out, and they carry no native-build advantage over upstream + the four patches **[I]**.
- **WineD3D standalone**: the only maintained native-Windows build is `adolfintel/wined3d4win`, whose author states plainly "I am not a Wine developer, I'm just maintaining these builds" **[V]**. A Vogons thread documents the dependency loop: `wined3d.dll` needs `opengl32.dll`, which on Microsoft's stack pulls in `ddraw.dll` **[V]**. On top of that, macOS caps OpenGL at 4.1 and Apple has deprecated it, so the target API is itself a dead end **[I]**.
- **D3DMetal**: proprietary, MacPorts lists it under a restrictive licence, and reporting says the terms appear to forbid using it in a shipping title; it covers D3D11/12 only **[V]**.
- **DXMT**: Metal-based, but its stated scope is D3D11 and D3D10 **[V]**.
- **gl4es**: a desktop-GL to GLES translator, unrelated to Direct3D **[V]**.
- **Gallium Nine Standalone**: a D3D9 frontend for Mesa, primarily targeting Linux with a Gallium driver **[V]**. KosmicKrisp is a Vulkan driver in Mesa, not a Gallium driver, so there is no path to feed Nine on macOS **[I]**.

---

## 3. Question 2 - the D3DX9 pieces

### 3.1 The chain, and it is verified in source

Wine already assembles exactly the chain we need, and the code is readable C.

**HLSL and `.fx` compilation goes to vkd3d-shader.** In `dlls/d3dx9_36/shader.c`, `D3DXCompileShader` calls `vkd3d_D3DCompile2VKD3D` directly for `D3DX_SDK_VERSION < 42` and `D3DCompile` otherwise, and forces `D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY` when `D3DX_SDK_VERSION <= 36` **[V]**. Our game imports `d3dx9_41.dll`, so it lands on the `D3DCompile` branch - and in Wine, `d3dcompiler` is itself backed by vkd3d-shader **[V]**.

**Effect source compilation is a single `D3DCompile` call with the `fx_2_0` profile.** `dlls/d3dx9_36/effect.c` lines 6723-6724 **[V]**:

```c
hr = D3DCompile(data, data_size, NULL, defines, include,
        NULL, "fx_2_0", compile_flags, 0, &bytecode, &temp_errors);
```

The function first reads a tag and dispatches to `d3dx9_effect_init_from_binary` when the data is already a compiled effect; otherwise it logs "HLSL ASCII effect, trying to compile it" and takes the path above **[V]**. So `D3DXCreateEffectFromFileA` on a `.fx` text file resolves to: compile to an `fx_2_0` blob, then parse that blob with Wine's own effect parser.

**vkd3d-shader supports the needed formats.** Its stated source formats are HLSL, DXIL, DXBC/TPF and legacy Direct3D bytecode; its target formats are SPIR-V, GLSL, **MSL** and Direct3D assembly, through `vkd3d_shader_compile()` and `vkd3d_shader_scan()` **[V]**. The `fx` target for `fx_2_0`, `fx_4_0`, `fx_4_1` and `fx_5_0` profiles arrived in vkd3d **1.11** **[V]**; disassembly of binary `fx_2_0` came in **1.16** **[V]**; vkd3d **1.12** added shader-model-1 code-generation fixes and `d3d-asm` as an HLSL target **[V]**; Giovanni Mascellani's freedesktop talk lists HLSL to SM1 as a supported path **[V]**. The latest release is **2.1 (2026-08-24)**, which added a dead-store-elimination pass aimed at matching the Direct3D compiler's **constant table layout**, stricter validation, more legacy D3D bytecode instructions, and reflection coverage extended to the D3D10 and D3D11 interfaces **[V]**.

That constant-table-layout work matters to us directly: the engine's `HLSLShaderBuilder` will set constants by the names the constant table reports **[I]**.

**`D3DXGetShaderConstantTable` is self-contained.** Wine's `shader.c` finds the `CTAB` comment chunk in the shader bytecode and parses it into its own `struct ctab_constant` tree; it does not call out to a compiler **[V]**. It is pure byte-pushing over a documented blob, so it ports without a graphics dependency **[I]**.

### 3.2 Recommended combination

| Need | Source | Note |
|---|---|---|
| HLSL -> SM1/SM2/SM3 bytecode (`vs_1_1` ... `ps_3_0`) | `libvkd3d-shader` **[V]** | Feed the bytecode straight to DXVK's `CreateVertexShader`/`CreatePixelShader`; DXVK translates legacy bytecode to SPIR-V internally **[I]** |
| HLSL `.fx` -> `fx_2_0` blob | `libvkd3d-shader` `fx` target, 1.11+ **[V]** | Called through the same `D3DCompile` entry point Wine uses **[V]** |
| `ID3DXEffect` run time | Port Wine `dlls/d3dx9_36/effect.c`, 7240 lines **[V]** | Parses the `fx_2_0` blob, holds parameters, runs technique passes |
| `D3DXGetShaderConstantTable` | Port Wine `shader.c` CTAB parser, 3020 lines total **[V]** | Self-contained **[V]** |
| HLSL -> MSL (fallback only) | `libvkd3d-shader` MSL target **[V]** | Experimental in 1.14, then texture loads in 1.16, sampling/gather/loops/switches and DXIL-to-MSL in 1.17, compute plus direct HLSL-to-MSL in 1.18 **[V]** |

**DirectXShaderCompiler is not usable.** DXC targets shader model 6.0 and above; it cannot emit the SM1-3 bytecode that a D3D9 device consumes **[I]**. **Mesa** supplies no D3D9 HLSL compiler; its D3D9 story is Gallium Nine, which is a state-level frontend, not a shader compiler, and is Linux-only **[V]**.

### 3.3 Porting cost of Wine's d3dx9, measured

`dlls/d3dx9_36/Makefile.in` **[V]**:

```
IMPORTS      = d3d9 d3dcompiler dxguid d3dxof ole32 gdi32 user32 wined3d
DELAYIMPORTS = windowscodecs
```

Mapped against the game's 13 `d3dx9_41` imports **[I]**:

| Import | Wine file | Host dependency we must replace |
|---|---|---|
| `D3DXGetShaderConstantTable` | `shader.c` (3020 lines) | None. Pure bytecode parsing **[V]** |
| `D3DXCreateEffectFromFileA` | `effect.c` (7240 lines) | `D3DCompile` -> vkd3d-shader. Plus file I/O **[V]** |
| `D3DXCreateTextureFromFileInMemoryEx` | `texture.c` (2239), `surface.c` (880), `d3dx_helpers.c` (3542) | **WIC.** 12 `IWICImagingFactory`/`wincodec` references, all in `d3dx_helpers.c` **[V]**. DDS and raw formats are handled in-tree (`bcdec.h`, `stb_dxt.h` are vendored **[V]**) |
| `D3DXCreateCubeTextureFromFileInMemory` | same | Same WIC surface **[V]** |
| `D3DXCreateFontA` | `font.c` (987 lines) | **GDI.** 15 references to `CreateFontIndirect`/`GetGlyphOutline`/`HDC`/`SelectObject` **[V]**. Needs a CoreText or FreeType replacement |
| `D3DXCreateRenderToSurface` | `render.c` (797 lines) | None beyond D3D9 itself **[V]** |

Good news in that table: I found **no direct `wined3d` references** in `core.c`, `effect.c`, `shader.c`, `surface.c`, `texture.c`, `font.c`, `render.c` or `d3dx_helpers.c` **[V]**, so the `wined3d` entry in `IMPORTS` looks like a format-helper import library rather than a runtime coupling **[I]**. `ole32` is needed only for COM plumbing we are writing anyway, and `d3dxof` only for `.x` mesh files, which are not in our import list **[V]**.

**So the real D3DX9 work is three replacements, not a port of the whole DLL [I]:** a WIC-shaped image decoder (the game only needs in-memory decode of whatever texture formats the assets use), a font rasteriser, and file I/O. Total Wine code worth taking: roughly 15,000 lines across `effect.c`, `shader.c`, `texture.c`, `surface.c`, `render.c` and `d3dx_helpers.c` **[V]**.

**Licence note [I]:** Wine's d3dx9 and vkd3d are both **LGPL-2.1-or-later**, confirmed for vkd3d from its README and the ALT Linux package metadata **[V]**. That is compatible with a personal archival project, but it means the vkd3d-shader library wants to be a dynamic `.dylib`, and any Wine code we copy into our tree makes that file LGPL. DXVK's zlib licence imposes no such constraint. Keeping the LGPL code in one clearly marked directory is the cheap way to stay clean.

---

## 4. Question 3 - precedent

### 4.1 Direct precedent exists, and it chose DXVK Native

**`codepdbh/nfsu2-recompiled`** is the closest match found. It is "a full static binary recompilation of Need for Speed: Underground 2 (`SPEED2.EXE`, x86-32) into clean, portable C code" - a Windows D3D9 game, same shape as ours **[V]**. Its Android path is "full Direct3D 9 to Vulkan translation powered by an embedded port of DXVK Native", on arm64-v8a **[V]**. Issue #901, opened 2026-10-18, is a community **native macOS Apple Silicon and Intel port** describing the path as "Direct3D 9 -> Vulkan -> Apple Metal" with "(no Wine / CrossOver)", using DXVK Native plus MoltenVK, at 60 FPS in native resolution with SDL2 gamepad support **[V]**.

That is our exact architecture, already shipped by someone else: static recomp of a 32-bit Windows D3D9 exe, running native on Apple Silicon, D3D9 served by DXVK Native **[I]**. The issue carries no build flags or patch list, so the engineering detail has to come from the kisakcod work in section 2.2 **[V]**.

Two further macOS/DXVK data points, both from pull requests: `arlyon/wowdecomp` PR #6 builds a native arm64 macOS client with DXVK/MoltenVK, and `himdo/Fable-2-Recomp` PR #93 adds Apple Silicon builds with MoltenVK compatibility **[V]**. I did not read either in full.

### 4.2 The N64Recomp family: right structure, wrong problem

| Project | Graphics approach | macOS |
|---|---|---|
| N64Recomp / Zelda64Recomp | **RT64**, a purpose-built renderer with D3D12 and Vulkan backends **[V]** | Zelda64Recomp's own listing names Windows and Linux; a mirror states D3D12 12.0 or Vulkan 1.2 **[V]** |
| CBFD-Recompiled | N64Recomp + RT64 | **Ships macOS, Apple Silicon, macOS 15+, rendered by RT64 on Metal** **[V]**. README notes it has not been played through on macOS and Intel is untested **[V]** |
| Star Fox 64 recomp | N64Recomp + RT64 | Developer states Windows, Linux (ARM and x86) and macOS "from the get-go" **[V]** |
| XenonRecomp / UnleashedRecomp | CPU via XenonRecomp; **shaders converted offline** by XenosRecomp to HLSL, then to DXIL and SPIR-V with DXC, for D3D12 or Vulkan **[V]**. Vertex declarations become native input declarations to avoid runtime shader permutations **[V]** | Community-driven, not on the roadmap, but the build system supports it; Vulkan via a **MoltenVK submodule** under `thirdparty/MoltenVK`; deployment target 13.0 **[V]**. XenonRecomp itself is x86-only because of x86 intrinsics **[V]** |
| xboxrecomp / `sp00nznet/hl2-recomp` | Runtime provides a "kernel shim, **D3D8 to D3D11**, DirectSound, input" **[V]** | Windows only; MSVC 2022 and CMake **[V]**. Reporting says the GPU side was taken from xemu and one game runs at 2-4 FPS **[V]** |

**The structural lesson [I]:** every one of these projects owns its GPU layer because the guest GPU was a console part with no PC API to honour. We are in the opposite position - the guest calls a real, documented, still-implemented PC API. Copying RT64's structure would mean writing a D3D9 implementation, which is the work DXVK already did. The one precedent that shares our starting point, NFSU2, linked DXVK Native. Follow that.

**The one idea worth borrowing from XenonRecomp [I]:** it converts shaders offline and avoids runtime shader permutation compilation entirely **[V]**. We cannot do that wholesale, because `HLSLShaderBuilder`/`ShaderBuilderGen` generate HLSL at run time. But the exe's `-DumpShaders` flag is a lever: dump every shader the game generates across a full playthrough, cache the compiled bytecode keyed by source hash, and runtime compilation becomes a cold-start cost rather than a per-frame one **[I]**.

---

## 5. Risks

**Biggest risk: the `fx_2_0` round trip.** Three things must all hold, and none is verified for this game's shaders. First, vkd3d-shader must compile this game's `.fx` source to an `fx_2_0` blob. Second, Wine's `effect.c` parser must accept that blob. Third, the constant-table layout vkd3d produces must match what `HLSLShaderBuilder` expects when it looks constants up by name. vkd3d 2.1's dead-store-elimination pass was written specifically to match the Direct3D compiler's constant table layout **[V]**, which tells us the two did not previously agree and that this is a live area **[I]**. The cheap early test: extract the HLSL and `.fx` text from the exe, compile each one with `vkd3d_shader_compile` on the laptop, and diff the constant tables against a reference. That is a half-day of work and it de-risks the single most expensive assumption **[I]**.

**Second risk: the MoltenVK texture bug.** Two sources disagree. The kisakcod work found every texture sampled black on MoltenVK and moved to KosmicKrisp **[V]**; the NFSU2 macOS port claims the MoltenVK stack works **[V]**. Mitigation is already in the recommendation: target KosmicKrisp, where sampling was verified byte-exact, and treat MoltenVK as the fallback rather than the plan **[I]**.

**Third risk: upstream will not take the macOS patches.** PR #5962 has been open since 2026-10-09 with no review and no maintainer statement on macOS support **[V]**, and `-Dnative_macos` is not even in it **[V]**. Plan for carrying a 4-patch fork indefinitely. The patches are small and mechanical, so rebasing is cheap **[I]**.

**Fourth risk: `D3DXCreateFontA`.** It is in the import list **[V]** and Wine's implementation is GDI-based **[V]**. There is no shortcut; it needs a CoreText or FreeType rasteriser behind the same interface. Scope it as its own slice.

**What would prove this plan wrong [I]:** the `.fx` test above failing in a way vkd3d cannot be made to handle; or DXVK's D3D9 refusing a capability the nu2 engine requires and checks (`D3DCAPS9` gating, `ProcessVertices`, or a surface format the engine demands).

---

## 6. Sources

DXVK and native builds
- https://github.com/doitsujin/dxvk - README, native section, `DXVK_WSI_DRIVER`
- https://raw.githubusercontent.com/doitsujin/dxvk/master/LICENSE - zlib/libpng
- https://github.com/doitsujin/dxvk/pull/5962 - macOS build fixes, open 2026-10-09
- https://github.com/jm2/kisakcod/pull/589 - macOS DXVK D3D9 build, merged 2026-10-09, M1 Max
- https://github.com/jm2/dxvk/blob/kisakcod-macos/KISAK_MACOS.md - the four patches, `-Dnative_macos`
- https://packages.debian.org/en/sid/libdxvk-native-dev - DXVK 2.7.1, D3D8/9/10/11 native
- https://github.com/Gcenx/DXVK-macOS and https://github.com/metalsharp/DXVK-MacOS - Wine PE DLL forks
- https://www.gamingonlinux.com/2021/08/dxvk-native-gets-an-official-release-to-help-linux-ports-with-vulkan

Vulkan drivers on macOS
- https://www.lunarg.com/lunarg-achieves-vulkan-1-3-conformance-with-kosmickrisp-on-apple-silicon/
- https://www.phoronix.com/news/KosmicKrisp-Vulkan-1.4-2026
- https://www.phoronix.com/news/KosmicKrisp-Vulkan-Metal

Shader and effect compilation
- https://raw.githubusercontent.com/wine-mirror/wine/master/dlls/d3dx9_36/effect.c - line 6723, `fx_2_0`
- https://raw.githubusercontent.com/wine-mirror/wine/master/dlls/d3dx9_36/shader.c - `vkd3d_D3DCompile2VKD3D`, CTAB parser
- https://raw.githubusercontent.com/wine-mirror/wine/master/dlls/d3dx9_36/Makefile.in - imports
- https://codeberg.org/vkd3d/vkd3d.git - libvkd3d-shader formats, LGPL-2.1
- https://www.linuxcompatible.org/story/vkd3d-111-released/ - `fx` target
- https://www.linuxcompatible.org/story/vkd3d-112-released/ - SM1 codegen
- https://phoronix.com/news/VKD3D-2.1-Released - 2026-08-24, constant table layout
- https://linuxiac.com/wines-vkd3d-2-1-improves-hlsl-support-and-shader-reflection/
- https://phoronix.com/news/VKD3D-1.14-Released - first MSL output
- https://indico.freedesktop.org/event/2/contributions/93/attachments/75/120/vkd3d-shader.pdf

Precedent
- https://github.com/codepdbh/nfsu2-recompiled/issues/901 - native macOS arm64, DXVK Native + MoltenVK, 2026-10-18
- https://raw.githubusercontent.com/codepdbh/nfsu2-recompiled/HEAD/README.md - D3D9 x86-32 static recomp, Android DXVK Native
- https://raw.githubusercontent.com/sciaschi/CBFD-Recompiled/HEAD/README.md - RT64 Metal on Apple Silicon
- https://github.com/hedge-dev/XenosRecomp and https://mintlify.wiki/hedge-dev/UnleashedRecomp/building/macos
- https://raw.githubusercontent.com/sp00nznet/hl2-recomp/HEAD/README.md - D3D8 to D3D11, Windows only
- https://github.com/arlyon/wowdecomp/pull/6 and https://github.com/himdo/Fable-2-Recomp/pull/93

Rejected options
- https://wined3d.fdossena.com/ and https://github.com/adolfintel/wined3d4win
- https://docs.mesa3d.org/gallium-nine.html
- https://ports.macports.org/port/d3dmetal/ and https://colincornaby.me/2023/06/mac-games-at-wwdc-2023
- https://github.com/3Shain/dxmt
- https://github.com/ptitSeb/gl4es

## Lead's verification, 2026-10-24

Checked by direct search, not taken on trust:

- **KosmicKrisp: confirmed.** LunarG's Vulkan-on-Metal driver, inside Mesa,
  MIT. Vulkan 1.3 conformant in 2025 and Vulkan 1.4 conformant in 2026,
  implemented on Metal 4, which needs macOS 26 or newer. Sources: LunarG's
  announcements, the Mesa driver docs, Phoronix.
- **The DXVK-on-M1 patch set and the NFSU2 recompilation precedent: not
  confirmed.** A direct search found neither. They stay in this document as
  leads, and no decision may rest on them until someone opens the repository
  and reads it.
- **Counter-signal found.** A forum report says DXVK's D3D9 path never worked
  on MoltenVK, because D3D9 there uses a Vulkan binding model MoltenVK does not
  support. Anecdotal, but it is the first thing the triangle test must disprove.

The recommendation still stands on its own merits: DXVK Native is real, is how
Proton ships native Linux builds, and KosmicKrisp gives it a conformant Vulkan
on this machine. The fallback (D3D9 to OpenGL with Cg) has the stronger local
precedent, because Feral shipped this engine on the Mac that way.
