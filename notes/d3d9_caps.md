# Direct3D 9 features LEGO Harry Potter needs, checked on DXVK Native + KosmicKrisp

Date: 2026-10-24. Static reading of `orig.nosync/lhp_win32.exe` (capstone, nothing executed) and a host probe on this Mac (`tools/d3d9_caps/probe.c`).
Scripts and raw output: `<job tmp>/d3d9_caps/` (`scan.py`, `xref.py`, `magic.py`, `dis.py`, `run2.log`; job scratch, not durable).

## Verdict

No blocker found. DXVK on KosmicKrisp answers every check the game makes with the answer its best path needs, and every FOURCC on that path works when used: INTZ gives raw depth, D24S8 gives a hardware depth compare, NULL works as a colour target, occlusion queries count, Reset works.
Only difference: MSAA 8x and 16x are not available (KosmicKrisp `framebufferColorSampleCounts` = 1, 2, 4). The game falls back to a lower entry of its MSAA table. This is a hardware/driver limit, not a DXVK option.

## 1. Methods the game calls

Method: `scan.py` follows `mov r,[0x10b0ed4]` (device) or `[0x10b0ed0]` (IDirect3D9), then `[r]` (vtable), then `call [vt+off]`, in every Ghidra function, and also in the 32 functions called with `ecx = 0x10b0ed0`. Counts are a lower bound: calls through a device pointer held in another register (for example `[esi+4]` in `D3DCreateDevice` 0xa37d90) are not all seen. The IDirect3D9 calls in `D3DCreateDevice` and `D3DCheckDeviceFormats` 0x93dee0 were read by hand (sections 2 and 5; `docs/STARTUP_REQS.md` section 4).
0x96b160..0x96c3ff is a block of thin wrappers, one per device method (no direct callers; reached through a table). A method seen only there may never run.

| Interface | Slot (offset) | Method | Sites | Site addresses (first 4) | Note |
|---|---|---|---|---|---|
| IDirect3D9 | 5 (0x14) | GetAdapterIdentifier | 1 | 0xa2c825 |  |
| IDirect3D9 | 6 (0x18) | GetAdapterModeCount | 1 | 0xa3a180 |  |
| IDirect3D9 | 7 (0x1c) | EnumAdapterModes | 1 | 0xa3a1fe |  |
| IDirect3D9 | 8 (0x20) | GetAdapterDisplayMode | 2 | 0x93d51c, 0xa2c838 |  |
| IDirect3D9 | 10 (0x28) | CheckDeviceFormat | 1 | 0x93d550 |  |
| IDirect3D9 | 14 (0x38) | GetDeviceCaps | 1 | 0xa37e39 |  |
| IDirect3DDevice9 | 3 (0xc) | TestCooperativeLevel | 3 | 0x93d5eb, 0xa383d0, 0xa38a10 |  |
| IDirect3DDevice9 | 4 (0x10) | GetAvailableTextureMem | 1 | 0x96b60b | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 5 (0x14) | EvictManagedResources | 4 | 0x96b5eb, 0xa38412, 0xa38473, 0xa3893f |  |
| IDirect3DDevice9 | 7 (0x1c) | GetDeviceCaps | 1 | 0x93d506 |  |
| IDirect3DDevice9 | 8 (0x20) | GetDisplayMode | 1 | 0x96b6d5 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 9 (0x24) | GetCreationParameters | 1 | 0x96b670 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 10 (0x28) | SetCursorProperties | 1 | 0x96bc3a | only in wrapper block 0x96b160..0x96c1e0; **rare** |
| IDirect3DDevice9 | 11 (0x2c) | SetCursorPosition | 3 | 0x93d5ba, 0x93d5d7, 0x945981 | **rare** |
| IDirect3DDevice9 | 12 (0x30) | ShowCursor | 1 | 0x96c385 | **rare** |
| IDirect3DDevice9 | 13 (0x34) | CreateAdditionalSwapChain | 1 | 0x96b1e5 | only in wrapper block 0x96b160..0x96c1e0; **rare** |
| IDirect3DDevice9 | 14 (0x38) | GetSwapChain | 1 | 0x96b9f5 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 15 (0x3c) | GetNumberOfSwapChains | 1 | 0x96b7bb | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 17 (0x44) | Present | 1 | 0xa389f1 |  |
| IDirect3DDevice9 | 18 (0x48) | GetBackBuffer | 1 | 0x9bb22a |  |
| IDirect3DDevice9 | 19 (0x4c) | GetRasterStatus | 1 | 0x96b8b5 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 20 (0x50) | SetDialogBoxMode | 1 | 0x96bc60 | only in wrapper block 0x96b160..0x96c1e0; **rare** |
| IDirect3DDevice9 | 21 (0x54) | SetGammaRamp | 3 | 0x93d654, 0x95f59a, 0x9c2fb2 | **rare** |
| IDirect3DDevice9 | 22 (0x58) | GetGammaRamp | 1 | 0x93d672 |  |
| IDirect3DDevice9 | 23 (0x5c) | CreateTexture | 3 | 0x96b380, 0x985010, 0x9f6e36 |  |
| IDirect3DDevice9 | 24 (0x60) | CreateVolumeTexture | 1 | 0x96b445 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 25 (0x64) | CreateCubeTexture | 2 | 0x96b21b, 0x9848f6 |  |
| IDirect3DDevice9 | 26 (0x68) | CreateVertexBuffer | 8 | 0x96b3c4, 0x986730, 0x9867ca, 0x98695b |  |
| IDirect3DDevice9 | 27 (0x6c) | CreateIndexBuffer | 5 | 0x96b2a7, 0x9868dd, 0x9869ee, 0x990a12 |  |
| IDirect3DDevice9 | 29 (0x74) | CreateDepthStencilSurface | 1 | 0x96b263 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 30 (0x78) | UpdateSurface | 2 | 0x96c3ff, 0x9d1737 | **rare** |
| IDirect3DDevice9 | 31 (0x7c) | UpdateTexture | 1 | 0x96c425 |  |
| IDirect3DDevice9 | 32 (0x80) | GetRenderTargetData | 4 | 0x96b918, 0x9c3168, 0x9f6eda, 0xa060b3 |  |
| IDirect3DDevice9 | 33 (0x84) | GetFrontBufferData | 1 | 0x96b6f8 | only in wrapper block 0x96b160..0x96c1e0; **rare** |
| IDirect3DDevice9 | 34 (0x88) | StretchRect | 5 | 0x96c3d7, 0x96c908, 0x96c93d, 0x9864c3 |  |
| IDirect3DDevice9 | 35 (0x8c) | ColorFill | 1 | 0x96b1bd | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 36 (0x90) | CreateOffscreenPlainSurface | 1 | 0x96b2dc | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 38 (0x98) | GetRenderTarget | 1 | 0x96b8f8 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 39 (0x9c) | SetDepthStencilSurface | 5 | 0x96c798, 0x96c7ea, 0x9b8710, 0x9c026f |  |
| IDirect3DDevice9 | 40 (0xa0) | GetDepthStencilSurface | 2 | 0x96b6b3, 0x9bb239 |  |
| IDirect3DDevice9 | 41 (0xa4) | BeginScene | 1 | 0x9d41ff |  |
| IDirect3DDevice9 | 42 (0xa8) | EndScene | 2 | 0x96b5ae, 0x986ed6 |  |
| IDirect3DDevice9 | 43 (0xac) | Clear | 4 | 0x96b18f, 0x9c03d4, 0x9d1676, 0xa38692 |  |
| IDirect3DDevice9 | 44 (0xb0) | SetTransform | 2 | 0x96c228, 0x997a6f |  |
| IDirect3DDevice9 | 45 (0xb4) | GetTransform | 1 | 0x96ba68 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 46 (0xb8) | MultiplyTransform | 1 | 0x96bb78 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 47 (0xbc) | SetViewport | 2 | 0x93e395, 0x9980b0 |  |
| IDirect3DDevice9 | 49 (0xc4) | SetMaterial | 1 | 0x96bd13 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 50 (0xc8) | GetMaterial | 1 | 0x96b793 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 51 (0xcc) | SetLight | 1 | 0x96bcf8 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 52 (0xd0) | GetLight | 1 | 0x96b758 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 53 (0xd4) | LightEnable | 1 | 0x96bb58 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 54 (0xd8) | GetLightEnable | 1 | 0x96b778 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 55 (0xdc) | SetClipPlane | 1 | 0x96bbd8 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 56 (0xe0) | GetClipPlane | 1 | 0x96b638 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 57 (0xe4) | SetRenderState | 371 | 0x96bf27, 0x96cc9e, 0x96ccee, 0x974c5d |  |
| IDirect3DDevice9 | 58 (0xe8) | GetRenderState | 1 | 0x96b8d8 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 59 (0xec) | CreateStateBlock | 1 | 0x96b348 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 61 (0xf4) | EndStateBlock | 1 | 0x96b5d3 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 62 (0xf8) | SetClipStatus | 1 | 0x96bbf3 | only in wrapper block 0x96b160..0x96c1e0; **rare** |
| IDirect3DDevice9 | 63 (0xfc) | GetClipStatus | 1 | 0x96b653 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 64 (0x100) | GetTexture | 1 | 0x96ba18 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 65 (0x104) | SetTexture | 28 | 0x96c1d7, 0x984562, 0x9845a3, 0x984f0d |  |
| IDirect3DDevice9 | 66 (0x108) | GetTextureStageState | 1 | 0x96ba3d | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 67 (0x10c) | SetTextureStageState | 3 | 0x96c1fd, 0x997dbe, 0x997dd9 |  |
| IDirect3DDevice9 | 68 (0x110) | GetSamplerState | 1 | 0x96b93d | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 69 (0x114) | SetSamplerState | 80 | 0x96c0c5, 0x96ce29, 0x985c84, 0x985cf7 |  |
| IDirect3DDevice9 | 70 (0x118) | ValidateDevice | 1 | 0x96c443 |  |
| IDirect3DDevice9 | 71 (0x11c) | SetPaletteEntries | 1 | 0x96bd58 | only in wrapper block 0x96b160..0x96c1e0; **rare** |
| IDirect3DDevice9 | 72 (0x120) | GetPaletteEntries | 1 | 0x96b7e8 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 73 (0x124) | SetCurrentTexturePalette | 1 | 0x96bc13 | only in wrapper block 0x96b160..0x96c1e0; **rare** |
| IDirect3DDevice9 | 74 (0x128) | GetCurrentTexturePalette | 1 | 0x96b693 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 75 (0x12c) | SetScissorRect | 2 | 0x96c0e3, 0x985558 |  |
| IDirect3DDevice9 | 76 (0x130) | GetScissorRect | 1 | 0x96b963 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 77 (0x134) | SetSoftwareVertexProcessing | 2 | 0x96c103, 0xa387b6 | **rare** |
| IDirect3DDevice9 | 78 (0x138) | GetSoftwareVertexProcessing | 1 | 0x96b97e | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 79 (0x13c) | SetNPatchMode | 1 | 0x96bd36 | only in wrapper block 0x96b160..0x96c1e0; **rare** |
| IDirect3DDevice9 | 81 (0x144) | DrawPrimitive | 5 | 0x96b51d, 0x986cb8, 0x986d5f, 0x991210 |  |
| IDirect3DDevice9 | 82 (0x148) | DrawIndexedPrimitive | 6 | 0x96b48c, 0x986b65, 0x990ed9, 0x990f05 |  |
| IDirect3DDevice9 | 83 (0x14c) | DrawPrimitiveUP | 2 | 0x96b56d, 0x987e8d |  |
| IDirect3DDevice9 | 84 (0x150) | DrawIndexedPrimitiveUP | 1 | 0x96b4f1 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 85 (0x154) | ProcessVertices | 1 | 0x96bbac | only in wrapper block 0x96b160..0x96c1e0; **rare** |
| IDirect3DDevice9 | 86 (0x158) | CreateVertexDeclaration | 11 | 0x96b3e8, 0x9914c6, 0x99772e, 0x9a33a8 |  |
| IDirect3DDevice9 | 87 (0x15c) | SetVertexDeclaration | 7 | 0x96c25a, 0x974acc, 0x986c2a, 0x987e52 |  |
| IDirect3DDevice9 | 88 (0x160) | GetVertexDeclaration | 1 | 0x96ba83 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 89 (0x164) | SetFVF | 1 | 0x96bc8d | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 90 (0x168) | GetFVF | 1 | 0x96b713 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 91 (0x16c) | CreateVertexShader | 5 | 0x96b408, 0x9a330d, 0xa026f7, 0xa04e9f |  |
| IDirect3DDevice9 | 92 (0x170) | SetVertexShader | 5 | 0x96c289, 0x993b5e, 0x99d263, 0x99d2b9 |  |
| IDirect3DDevice9 | 93 (0x174) | GetVertexShader | 1 | 0x96baa3 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 94 (0x178) | SetVertexShaderConstantF | 87 | 0x96c2ed, 0x96c6aa, 0x96c6dc, 0x96c6fb |  |
| IDirect3DDevice9 | 95 (0x17c) | GetVertexShaderConstantF | 1 | 0x96bafd | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 96 (0x180) | SetVertexShaderConstantI | 1 | 0x96c31d |  |
| IDirect3DDevice9 | 97 (0x184) | GetVertexShaderConstantI | 1 | 0x96bb2d | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 98 (0x188) | SetVertexShaderConstantB | 1 | 0x96c2bd | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 99 (0x18c) | GetVertexShaderConstantB | 1 | 0x96bacd | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 100 (0x190) | SetStreamSource | 13 | 0x96c174, 0x986aec, 0x986c89, 0x986d16 |  |
| IDirect3DDevice9 | 101 (0x194) | GetStreamSource | 1 | 0x96b9b2 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 102 (0x198) | SetStreamSourceFreq | 5 | 0x96c198, 0x986b0a, 0x9e2b00, 0x9e30d0 |  |
| IDirect3DDevice9 | 103 (0x19c) | GetStreamSourceFreq | 1 | 0x96b9d8 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 104 (0x1a0) | SetIndices | 4 | 0x96bcca, 0x986b3b, 0x990e50, 0x990e9b |  |
| IDirect3DDevice9 | 105 (0x1a4) | GetIndices | 1 | 0x96b733 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 106 (0x1a8) | CreatePixelShader | 8 | 0x96b308, 0x9a332c, 0x9a334b, 0x9a336a |  |
| IDirect3DDevice9 | 107 (0x1ac) | SetPixelShader | 5 | 0x96bd79, 0x993b80, 0x99d292, 0x99d2d5 |  |
| IDirect3DDevice9 | 108 (0x1b0) | GetPixelShader | 1 | 0x96b803 | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 109 (0x1b4) | SetPixelShaderConstantF | 59 | 0x96bddd, 0x971e7c, 0x971ea9, 0x976957 |  |
| IDirect3DDevice9 | 110 (0x1b8) | GetPixelShaderConstantF | 1 | 0x96b85d | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 111 (0x1bc) | SetPixelShaderConstantI | 1 | 0x96be0d | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 112 (0x1c0) | GetPixelShaderConstantI | 1 | 0x96b88d | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 113 (0x1c4) | SetPixelShaderConstantB | 1 | 0x96bdad | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 114 (0x1c8) | GetPixelShaderConstantB | 1 | 0x96b82d | only in wrapper block 0x96b160..0x96c1e0 |
| IDirect3DDevice9 | 118 (0x1d8) | CreateQuery | 2 | 0x96b328, 0x9f1481 |  |

Not seen by the scan: CreateRenderTarget, SetRenderTarget, DrawRectPatch/DrawTriPatch. A generic scan (any `call [vt+0x70]`) finds a CreateRenderTarget-shaped call at 0x9bb5bf with format 0x15 (A8R8G8B8); the interface there is not confirmed.
Flags for DXVK on macOS:
- `ProcessVertices`, `SetNPatchMode`, palettes, `SetDialogBoxMode`, `GetFrontBufferData`: only in the wrapper block. Not probed. **UNVERIFIED**: DXVK's `ProcessVertices` may need `geometryShader`, which KosmicKrisp lacks.
- `SetCursorProperties` / `ShowCursor`: DXVK prints `D3D9Cursor::ResetCursor: Not supported on current platform` on this build. Use an OS cursor.
- `SetGammaRamp` (3 sites, flags 1): brightness. Not probed.

## 2. Capability checks and what the game does on failure

| Call (site) | Exact arguments | On failure | DXVK on KosmicKrisp |
|---|---|---|---|
| CheckDeviceType (0xa37fd0) | HAL, display format, A8R8G8B8, Windowed | back buffer uses the display format | OK (windowed and fullscreen) |
| CheckDeviceMultiSampleType (0xa380fd, 0xa38134) | back-buffer format and D24S8, types from table 0xe9beb8 = {0, 2, 4, 8, 16}, loop of 5 | next table entry | 2x, 4x OK (quality 1); 6x, 8x D3DERR_NOTAVAILABLE |
| CheckDeviceFormat (0x93dfa1) loop 1 | X8R8G8B8, DEPTHSTENCIL, TEXTURE, {INTZ, RAWZ} | obj+0x6f4 = 0, +0x700 = 1 | INTZ OK, RAWZ no → +0x700 = 3 |
| CheckDeviceFormat (0x93e031) loop 2 | X8R8G8B8, DEPTHSTENCIL, TEXTURE, {D24S8, INTZ, DF24, RAWZ} | +0x6fc = D24S8, +0x704 = 1 | D24S8 OK → +0x704 = 0 (hardware shadow compare) |
| CheckDeviceFormat (0x93e143) loop 3, only if +0x704 != 1 | X8R8G8B8, RENDERTARGET, TEXTURE, {NULL, R5G6B5} | +0x6f8 = A8R8G8B8 | NULL OK → +0x6f8 = NULL |
| IDirect3D9::CheckDeviceFormat in 0x93d4f0 | caller arguments | - | dead code: no caller found (no call, jmp or pointer) |

0x93dee0 runs loops 1-3 only when all hold: obj+0x63c == 0, pcconfig `AllowVendorExtensions` > 0 (byte 0xed8912, setter 0x96fcd0), `TTAnimationMode` == 0 (0xed8934), `UseHires` != 0 (0xed8913; cleared at 0xa3d451 when the shader level is below 6, and at 0xa3d4ca for some old NVIDIA device IDs), and VendorId (obj+0x5d0) is 0x10DE or 0x1002. Otherwise it keeps the defaults (no FOURCC, D24S8, A8R8G8B8).
The option names come from the pcconfig.txt key table 0xed2e00..0xed2f70 (46 keys, read by 0x9f7420). All three bytes are 0 in the image. **OPEN:** the values a first run writes (vendor presets, `IgnoreVendorPresets`).
+0x708 (shadow-filter mode): NVIDIA: 2 if D24S8 else 0; AMD: 2 if D24S8 else 1; others 0. A final table at 0x93e15c accepts (+0x700, +0x704, +0x708) in {(1,1,0), (2,*,2), (3,*,2), (1,3,1), (1,*,2)}; anything else resets all six fields to the defaults.
Result on DXVK (reports AMD, see below): +0x6f4 INTZ, +0x700 3, +0x6fc D24S8, +0x704 0, +0x708 2, +0x6f8 NULL. (3, *, 2) is accepted.
Fetch4 (`GET4`/`GET1` in D3DSAMP_MIPMAPLODBIAS, 0x96cd70, 0x9feba2, 0x9ff38f) runs only when VendorId == 0x1002 and +0x708 == 1. On DXVK +0x708 is 2, so Fetch4 is not used. Not probed.
FOURCC values in code (magic.py, byte search of .text): INTZ, RAWZ, DF24, NULL (0x93dee0 only), GET4, GET1, DXT1-5. Not in code: DF16, RESZ (and 0x7fa05000), ATOC, A2M0/A2M1, INST, R2VB, NVDB, ATI1, ATI2. The game does not use the POINTSIZE / ADAPTIVETESS magic values. CheckDepthStencilMatch and CheckDeviceFormatConversion are never called.

## 3. D3DCAPS9 fields read (caps at 0x10b0edc = obj+0xc)

| Field (offset) | Sites | Test | DXVK value |
|---|---|---|---|
| VertexShaderVersion (0xc4) / PixelShaderVersion (0xcc) | 0xa2c8b2, 0xa2c8c0 (0xa2c8a0) | level 1 at 1.1, 3 at 2.0 (lowered to 2), 6 at 3.0; level 0 = fatal message box | 3.0 / 3.0 → level 6 |
| VertexShaderVersion | 0xa38063 | >= 0xfffe0101 → HARDWARE_VERTEXPROCESSING | yes |
| DevCaps (0x1c) | 0xa38078 | & 0x10000 HWTRANSFORMANDLIGHT → MIXED fallback | 1 |
| PresentationIntervals (0x14) | 0xa37e84 | vsync choice | 0x8000000f |
| TextureFilterCaps (0x40) | 0x93ae19, 0x93ae39, 0x99372a, 0x993799, 0xa297b6 | & 0x400 MINFANISOTROPIC, & 0x4000000 MAGFANISOTROPIC, only at level 6 → anisotropic filter, else linear | 1, 1 |
| MaxAnisotropy (0x6c) | 0x9931ac, 0xa2980a | clamp of the anisotropy setting | 16 |
| DeclTypes (0xec) | 0x9692c3, 0x9693a5 | & 0x100 FLOAT16_2, & 0x200 FLOAT16_4 → half vertex data, else float | 1, 1 |
| MaxSimultaneousTextures (0x98) | 0x997df3, 0x997f1a | sampler loop bound | 8 |
| TextureCaps (0x3c) | 0x9b0a49, 0x9f060e | & 2 POW2 or & 0x100 NONPOW2CONDITIONAL → power-of-two limits | 0, 0 (full NPOT) |
| StencilCaps (0x88) | 0x9f1c08 | & 0x100 TWOSIDED (render state 0xb9 TWOSIDEDSTENCILMODE is set) | 1 |

Also printed by the probe: NumSimultaneousRTs 4, MaxStreams 16, MaxVertexShaderConst 256, MaxTextureWidth 16384. Caps read through a register base (not an absolute address) are not all listed.

## 4. Render states, samplers, present parameters

Render states set with constant arguments (371 sites): ZENABLE, CULLMODE, ZFUNC, ALPHAREF/ALPHAFUNC, ALPHATESTENABLE, ALPHABLENDENABLE, SRC/DESTBLEND, BLENDOP, STENCIL* and two-sided CCW stencil (0xb9..0xbd), COLORWRITEENABLE (0xa8) and COLORWRITEENABLE2 (0xbf), SCISSORTESTENABLE (0xae), SEPARATEALPHABLENDENABLE + SRC/DEST/BLENDOPALPHA (0xce..0xd1), FOG* (0x22, 0x24..0x26), LIGHTING, MULTISAMPLEANTIALIAS (0xa1). Many values come from registers (state cache wrappers 0x987b10, 0x987c00, 0x985c20). None set POINTSIZE magic, SRGBWRITEENABLE or DEPTHBIAS with constants. **OPEN:** the register-valued states (for example SRGBWRITEENABLE, depth bias) are not resolved statically.
Samplers: MIN/MAG/MIPFILTER point/linear/anisotropic, ADDRESSU/V wrap/clamp, MAXANISOTROPY (0xa), Fetch4 values above. SetStreamSourceFreq (0, 1), (1, 0x80000001): geometry instancing (INSTANCEDATA | 1) — DXVK supports it; not probed. Occlusion queries: CreateQuery(9) at 0x9f1481. Two `SetClipPlane` and `SetScissorRect` sites take values from callers.
Present parameters (docs/STARTUP_REQS.md section 4): A8R8G8B8, 1 buffer (2 with vsync), DISCARD, D24S8 auto depth, DISCARDDEPTHSTENCIL, IMMEDIATE or ONE, windowed flag from config, MSAA from table 0xe9beb8 / `FSAA`; BehaviorFlags PUREDEVICE | HARDWARE_VERTEXPROCESSING (| MULTITHREADED with `ForceMultithreadedD3D`).

## 5. Lost device

- Present (0x44) at 0xa389f1 (0xa389c0). D3DERR_DEVICELOST → TestCooperativeLevel (0xa38a10); D3DERR_DEVICENOTRESET → `D3DCreateDevice(1)` (reset mode). D3DERR_DRIVERINTERNALERROR → MessageBoxA(MB_ICONHAND). It stores the result at obj+0x600.
- Reset mode in 0xa37d90: TestCooperativeLevel (0xa383d0) checks 0x88760827 / 0x88760868 / 0x88760869, then EvictManagedResources (0xa38412, 0xa38473) and Reset (`call [vt+0x40]`, 0xa38433, 0xa38494), then the engine re-creates its resources (0x9bb1e0 ... 0x9e2500).
- 0xa38930: EvictManagedResources, Reset(obj+0x13c), same re-create list. WM_SIZE goes through `D3DResizeDevice` 0x93e6a0 (mode lookup only).
- DXVK windowed never reports lost; the probe's Reset with the game parameters returns S_OK and TestCooperativeLevel stays S_OK.

## 6. Vendor identity

DXVK reports VendorId 0x1002, DeviceId 0x73df, "AMD Radeon RX 6700 XT". Cause: KosmicKrisp's vendor ID is not NVIDIA/AMD/Intel, so `src/d3d9/d3d9_adapter.cpp:1063-1083` (hp-dxvk) uses the AMD fallback unless `d3d9.customVendorId` or `dxgi.hideAmdGpu` is set. The game's vendor branches therefore take the AMD side: the depth-format gate passes, 0xa3851a calls AMD routine 0xa41a30 (**OPEN:** not analysed; it starts with GetVersionExA), and Fetch4 is off because +0x708 is 2. No change needed. To test the NVIDIA side, set `d3d9.customVendorId = 10de` in dxvk.conf (not done).

## 7. Host probe results (`tools/d3d9_caps/build.sh run`, 2026-10-24)

    adapter: AMD Radeon RX 6700 XT  VendorId 0x1002 DeviceId 0x73df
    GetDeviceCaps(0, HAL)                                      hr=0x00000000 OK
    caps VertexShaderVersion 0xfffe0300 PixelShaderVersion 0xffff0300
    caps DevCaps HWTRANSFORMANDLIGHT 1  PresentationIntervals 0x8000000f
    caps TextureFilterCaps MINFANISOTROPIC 1 MAGFANISOTROPIC 1  MaxAnisotropy 16
    caps DeclTypes FLOAT16_2 1 FLOAT16_4 1  MaxSimultaneousTextures 8
    caps TextureCaps POW2 0 NONPOW2CONDITIONAL 0  StencilCaps TWOSIDED 1
    caps NumSimultaneousRTs 4 MaxStreams 16 MaxVertexShaderConst 256 MaxTextureWidth 16384
    game shader level (0xa2c8a0 rule): 6 (needs 6 for vs_3_0/ps_3_0)
    display mode 2056x1329 @120 fmt 22; X8R8G8B8 mode count 88
    CheckDeviceType(HAL, display fmt, A8R8G8B8, windowed)      hr=0x00000000 OK
    CheckDeviceType(HAL, display fmt, A8R8G8B8, fullscreen)    hr=0x00000000 OK
    CheckDeviceMultiSampleType 2x: A8R8G8B8 hr=0x00000000 q=1  D24S8 hr=0x00000000 q=1
    CheckDeviceMultiSampleType 4x: A8R8G8B8 hr=0x00000000 q=1  D24S8 hr=0x00000000 q=1
    CheckDeviceMultiSampleType 6x: A8R8G8B8 hr=0x8876086a q=1  D24S8 hr=0x8876086a q=1
    CheckDeviceMultiSampleType 8x: A8R8G8B8 hr=0x8876086a q=1  D24S8 hr=0x8876086a q=1
    CheckDeviceFormat(X8R8G8B8, DEPTHSTENCIL, TEXTURE, INTZ)   hr=0x00000000 OK
    CheckDeviceFormat(X8R8G8B8, DEPTHSTENCIL, TEXTURE, RAWZ)   hr=0x8876086a no
    CheckDeviceFormat(X8R8G8B8, DEPTHSTENCIL, TEXTURE, D24S8)  hr=0x00000000 OK
    CheckDeviceFormat(X8R8G8B8, DEPTHSTENCIL, TEXTURE, DF24)   hr=0x00000000 OK
    CheckDeviceFormat(X8R8G8B8, RENDERTARGET, TEXTURE, NULL)   hr=0x00000000 OK
    CheckDeviceFormat(X8R8G8B8, RENDERTARGET, TEXTURE, R5G6B5) hr=0x00000000 OK
    CreateDevice(PUREDEVICE|HWVP, game present params)         hr=0x00000000 OK
    TestCooperativeLevel                                       hr=0x00000000 OK
    CreateVertexShader vs_3_0                                  hr=0x00000000 OK
    CreatePixelShader ps_3_0                                   hr=0x00000000 OK
    CreateVertexDeclaration                                    hr=0x00000000 OK
    CreateQuery(OCCLUSION)                                     hr=0x00000000 OK
    CreateTexture INTZ DEPTHSTENCIL 64x64                      hr=0x00000000 OK
    CreateRenderTarget NULL 64x64                              hr=0x00000000 OK
      INTZ sampled: left (ref 0.21) 0x40404040  right (ref 0.29) 0x40404040  red 64 / 64 (raw depth 0.25 = 64; compare = 255 / 0)
    CreateTexture D24S8 DEPTHSTENCIL 64x64                     hr=0x00000000 OK
    CreateRenderTarget NULL 64x64                              hr=0x00000000 OK
      D24S8 sampled: left (ref 0.21) 0xffffffff  right (ref 0.29) 0x00000000  red 255 / 0 (raw depth 0.25 = 64; compare = 255 / 0)
    occlusion query: hr=0x00000000 samples 307200 (640x480 = 307200)
    Present                                                    hr=0x00000000 OK
    EvictManagedResources                                      hr=0x00000000 OK
    Reset(game present params)                                 hr=0x00000000 OK
    TestCooperativeLevel after Reset                           hr=0x00000000 OK
    PASS d3d9_caps probe (0 differences)

The probe draws depth z = 0.25 into a 64x64 depth texture with a NULL colour target, then samples it with a vs_3_0/ps_3_0 pair; texcoord.z is the compare value (0.21 left, 0.29 right). INTZ returns the raw depth (64/255 = 0.25 both sides, no compare). D24S8 returns the compare result (255 where 0.21 <= 0.25, 0 where 0.29 > 0.25), as on NVIDIA hardware shadow maps.

## Needed actions

- None for the full-quality depth path.
- pcconfig.txt must hold `AllowVendorExtensions 1`, `UseHires 1`, `TTAnimationMode 0` for the INTZ/NULL path to run at all; check what the game writes on first run.
- MSAA: offer 2x/4x only; 8x/16x are not available on KosmicKrisp.
- Not probed: `ProcessVertices` (wrapper block only), Fetch4, instancing, SetGammaRamp, A16B16G16R16F and other render-target formats (formats come from registers; not resolved statically).
