# Packaging: Wizard Bricks Recompiled.app

The app was called "Harry Potter Recompiled" until 2026-10-25. The bundle id
(`local.harrypotterrecomp`), the launcher name and the save and log folders kept
their old names, so existing saves still load.

## Build

`./setup.sh` builds it. By hand, from the repo root, after the lift
(docs/BUILDING.md):

```sh
make -C build -j8 app HP_GAME_DIR="<game folder>"
```

`build/Makefile` builds the game binary `build.nosync/lhp` and passes the
prefixes to `tools/package/make_app.sh`: `DXVK_PREFIX`, `VKD3D_PREFIX` and
`KK_PREFIX` (default `build.nosync/deps/{dxvk,vkd3d,kosmickrisp}`, built by
tools/deps), `BREW_PREFIX`, `HP_GAME_DIR` and `HP_EXE` (default
`orig.nosync/lhp_win32.exe`). Output: `build.nosync/Wizard Bricks Recompiled.app`.

The 2026-10-24 app (built by hand from the boot build) was 91 MB: the launcher,
the game binary `lhp`, and 18 libraries in
`Contents/Frameworks` (DXVK d3d9, vkd3d-shader, FAudio, sdl2-compat, SDL3,
Vulkan loader, KosmicKrisp ICD and its dependencies). The KosmicKrisp ICD
manifest is in `Contents/Resources/vulkan/icd.d`. `lhp_win32.exe` is not
bundled. The launcher references it by the path in `HP_EXE`.
The app icon (`Contents/Resources/AppIcon.icns`) is made at build time by
`tools/package/exe_icon.py` from the icon in `LEGOHarryPotter.exe`, else `HP_EXE`.

`KK_PREFIX` is a KosmicKrisp build: Mesa 26.2.5 with the three patches in
`tools/package/patches` applied in this order: `kosmickrisp-drawable-release`,
`kosmickrisp-renderpass-release` (two leaks), `kosmickrisp-wait-for-drawable`
(upstream Mesa bfe08f7c: the queue waits until the display has released a
drawable before it renders to it). `tools/deps/build_kosmickrisp.sh` builds it.
Without `KK_PREFIX`, make_app.sh bundles Homebrew's unpatched KosmicKrisp.

Build outside `~/Documents`. iCloud's file provider sets `com.apple.FinderInfo`
on any bundle folder under `~/Documents`, `build.nosync` included, at once and
again after `xattr -c`. Then `codesign --verify --strict` fails with "Disallowed
xattr com.apple.FinderInfo". A copy in `build.nosync` passes a non-strict
`--deep` verify and launches.

## Launch

Double-click the app, or run
`"build.nosync/Wizard Bricks Recompiled.app/Contents/MacOS/HarryPotterRecompiled"`.

- Overrides: `HPRECOMP_GAME_DIR` (folder with `LEGOHarryPotter.exe`) and
  `HPRECOMP_EXE` (the game image).
- Log: `~/Library/Logs/HarryPotterRecomp/run-<stamp>.log`, newest 20 kept. If the
  game folder or the image is missing, the launcher writes the error to stderr
  and to that log and exits 1. It also shows an error dialog, unless
  `RECOMP_NO_ALERT_UI=1` is set (test runs set it so no dialog blocks them).
- Saves: `~/Library/Application Support/HarryPotterRecomp`.
- The launcher sets `VK_DRIVER_FILES` (bundled ICD), `DXVK_WSI_DRIVER=SDL2` and
  `DXVK_LOG_PATH`. It does not set `SDL_AUDIO_DRIVER`, so the game uses real audio.

## Verification (2026-10-24)

- `codesign --verify --deep --strict`: valid on disk, satisfies its Designated Requirement.
- `otool -L` and `LC_RPATH` on every Mach-O in the bundle: no `/opt/homebrew` or `/Users` path.
- 60 s launch of the app's launcher under `caffeinate -u -d` with
  `RECOMP_ASSUME_FOCUS=1 RECOMP_NO_ALERT_UI=1 SDL_AUDIO_DRIVER=dummy HP_TRACE_D3D9=1`
  (the 60 s alarm stopped it, exit 142): 6,304 `D3D9 Present` calls, all
  hr=00000000. The only `ERROR:` line was the gdi32 D3DKMT probe (boot_log run 15).
  It was not harmless: see notes/scene_change.md; 84d4f6e adds both exports.
  Log: `run-2026-10-24_060624.log`.

## Display options (launcher environment)

- Default: a borderless desktop-sized window, no display mode switch. The game
  renders at the desktop's scaled size below the notch (2056x1290 on a 16-inch
  MacBook Pro at "More Space"). Its resolution menu also offers the full panel
  size, 75% of it and 50% (listed at 60 Hz; the host still vsyncs at the
  display's rate). Ctrl+Cmd+F toggles a window.
- Frame-time overlay: Cmd+F shows or hides it; `RECOMP_HUD=1` shows it from
  the start. The game does not see the key. Same lines and graph as the
  Hobbit and Marvel overlays (the engine's hud_core.c; runtime/src/hp_hud.c
  draws them with the game's D3D9 device): `FPS`, `1% LOW`; `AVG`, `P99`,
  `MAX`; `OVER 33 MS` (frames of the last 240 over 33.3 ms); `GAME`, `SWAP`
  (time before and inside the host Present); `DRAWS`, `RT SETS` (D3D9 draw
  calls and render-target sets per frame, in place of GL draws and maps);
  `RENDER`, `WINDOW` (back buffer, window in points, ratio); `VSYNC`, `TSO`,
  `-O`, `HEAP GUARD`.
- Known issue: at the menu's other sizes (panel, 75%, 50%) the game lays out
  its picture and UI for a smaller area (seen at 3456x2168); only the default
  desktop size is verified.
- `HPRECOMP_WINDOWED=1`: a window the size of the back buffer, with the
  display's own mode list.
- `HPRECOMP_FULLSCREEN=1`: the game's exclusive fullscreen (a mode switch).
- Vsync is always on: the host presentation interval is ONE (FIFO) whatever
  the game's VerticalSync setting, so the in-game vsync option has no effect.
  Vsync off cannot work well here. The default window covers the whole
  display, so macOS scans it out directly, and KosmicKrisp's IMMEDIATE mode
  turns display sync off (`displaySyncEnabled = NO`,
  wsi_common_metal_layer.m:140,172): frames reach the screen mid-refresh and
  tear (measured: shown intervals 3.8-4.8 ms on a 120 Hz display).
  KosmicKrisp offers only IMMEDIATE and FIFO (wsi_common_metal.c:42-43), no
  MAILBOX, so DXVK's `dxvk.tearFree` cannot help.
- No frame cap: the display's refresh rate paces the game (120 Hz on a
  ProMotion display; measured 117 frames shown per second, all one vblank
  apart).
