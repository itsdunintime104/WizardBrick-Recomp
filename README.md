> [!IMPORTANT]
> This project does not include any game files. You need your own Steam copy of LEGO Harry Potter: Years 1-4 (the Windows version).

<h1 align="center">
  Wizard Bricks Recompiled
</h1>

<p align="center">
  <img src="https://img.shields.io/badge/platform-macOS%20Apple%20Silicon-black?logo=apple" alt="Platform: macOS on Apple Silicon">
  <img src="https://img.shields.io/badge/status-playable-brightgreen" alt="Status: playable">
  <img src="https://img.shields.io/badge/license-GPL--3.0-blue" alt="License: GPL-3.0">
</p>

LEGO Harry Potter: Years 1-4 (TT Games, 2010) was released on Steam for Windows only. This project translates the game's 32-bit Windows code to C ahead of time and compiles it into a native arm64 app for Apple Silicon. It does not use Wine or an emulator. Direct3D 9 calls go to [DXVK](https://github.com/doitsujin/dxvk), which draws with Vulkan on [KosmicKrisp](https://docs.mesa3d.org/drivers/kosmickrisp.html), Mesa's Vulkan driver for Metal.

The translator and runtime are [MacRecomp](https://github.com/itsdunintime104/MacRecomp), the same engine that runs [Hobbit Recompiled](https://github.com/itsdunintime104/HobbitRecompiled) and [Superheros Bricks Recompiled](https://github.com/itsdunintime104/SuperheroBrickRecomp). This game adds a Windows (PE) front end and a Win32 layer.

This project is not affiliated with or endorsed by TT Games, Warner Bros. Games, Wizarding World or the LEGO Group.

## Contents

- [Requirements](#requirements)
- [Get the game files](#get-the-game-files)
- [Install](#install)
- [Playing](#playing)
- [Shader warm-up and stutter](#shader-warm-up-and-stutter)
- [Saves](#saves)
- [Status](#status)
- [Performance](#performance)
- [Troubleshooting](#troubleshooting)
- [Building and development](#building-and-development)
- [Credits](#credits)
- [Support](#support)
- [License](#license)

## Requirements

- A Mac with Apple Silicon (M1 or newer)
- LEGO Harry Potter: Years 1-4 from Steam, Windows version, build 660 (Steam app 21130)
- Xcode Command Line Tools
- Homebrew with the packages listed in [Install](#install)
- Python 3.11 or newer
- About 3 GB of free disk space for the build, plus 6.3 GB for the game files

## Get the game files

The game has no Mac version, so Steam for Mac does not download it. Use one of these, all with your own Steam licence:

1. **SteamCMD** (no Windows PC needed). Download [SteamCMD for macOS](https://steamcdn-a.akamaihd.net/client/installer/steamcmd_osx.tar.gz), unpack it, and run:
   ```sh
   ./steamcmd.sh +@sSteamCmdForcePlatformType windows +force_install_dir "$HOME/Games/LEGO Harry Potter" +login <your Steam user> +app_update 21130 validate +quit
   ```
2. **A Windows PC**: install the game with Steam and copy the `LEGO Harry Potter` folder to your Mac.
3. **Steam inside Wine, CrossOver or Whisky**: `setup.sh` finds the game there by itself.

## Install

1. Install the Xcode Command Line Tools, and wait until the installer says it is done:
   ```sh
   xcode-select --install
   ```
2. Install [Homebrew](https://brew.sh), then the tools:
   ```sh
   brew install sdl2 sdl3 faudio vulkan-loader vulkan-headers meson ninja pkgconf glslang \
       llvm spirv-llvm-translator spirv-tools zstd autoconf automake libtool bison flex mingw-w64 python
   ```
3. Get the code and run the setup script:
   ```sh
   git clone --recurse-submodules https://github.com/itsdunintime104/WizardBrick-Recomp.git
   cd WizardBrick-Recomp
   HPRECOMP_GAME_DIR="$HOME/Games/LEGO Harry Potter" ./setup.sh
   ```
4. Open **LEGO Harry Potter Years 1-4** from `~/Applications`.

`setup.sh` checks your Mac, builds DXVK, vkd3d-shader and KosmicKrisp at pinned versions with this repo's patches (about 4 minutes, once), copies the game's exe, checks its sha256, translates it, builds the app and installs it. It never writes to the game folder. `./setup.sh --check` runs only the checks.

At the end, setup warms the game's shader cache: the game window opens for about 6 minutes and loads every level by itself. Leave it open; it closes by itself. This cuts most of the stutter, not all of it: see [Shader warm-up and stutter](#shader-warm-up-and-stutter). `--no-warm` skips this step.

Keep the cloned folder: the app loads the game's code from `orig.nosync/` in it. The app remembers the game folder you gave in `HPRECOMP_GAME_DIR`.

| Option | Effect |
|---|---|
| `--check` | Only check the requirements |
| `--force` | Translate again. Use it after `git pull` |
| `--no-install` | Build the app in `build.nosync/` without copying it to `~/Applications` |
| `--no-warm` | Do not build the shader cache |
| `--dry-run` | Print each step without running it |

## Playing

| Key | Action |
|---|---|
| W A S D | Move |
| J | Cast a spell |
| K | Switch character |
| Return | Menus |
| Esc | Pause menu |
| Cmd+F | Show or hide the performance overlay |
| Ctrl+Cmd+F | Switch between full screen and a window |
| Cmd+Q | Quit |

Controllers that SDL supports are passed to the game as Xbox controllers.

The game opens borderless at your display's scaled size and follows your display's refresh rate. Options:

| Environment variable | Effect |
|---|---|
| `HPRECOMP_WINDOWED=1` | A window the size of the game's render size |
| `HPRECOMP_FULLSCREEN=1` | The game's own exclusive full screen (changes the display mode) |
| `HPRECOMP_GAME_DIR` | Game folder, if it moved after setup |

Each run writes a log to `~/Library/Logs/HarryPotterRecomp/`. It holds startup messages, warnings and errors, not what you do in the game. The app keeps the newest 20. Attach the log when you report a problem.

## Shader warm-up and stutter

> [!NOTE]
> Expect some stutter in your first hours of play. It fades as you visit more of the game.

The Windows game compiles its shaders while you play. On a Mac each new shader then goes through DXVK and Apple's Metal compiler, and the game waits until that is done. The wait is a short hitch, or 1 to 3 seconds when a new level starts. A shader is compiled only once: it is kept in the caches below, so later visits are smooth.

The warm-up at the end of `setup.sh` compiles most shaders before your first play. It loads each level and draws one frame of it. Spells, effects, characters and cutscenes that this frame does not show are still compiled the first time you see them, so some short stutter remains at first.

| Cache | Where | Holds |
|---|---|---|
| Game shader cache | `~/Library/Application Support/HarryPotterRecomp/…/CachedShaders` | The game's compiled shaders |
| Apple Metal cache | `$(getconf DARWIN_USER_CACHE_DIR)local.harrypotterrecomp` | The GPU code Apple's compiler made; most of the compile time |
| DXVK cache | `~/.cache/dxvk` | DXVK's shader data |

Do not delete these caches. The stutter comes back until they are rebuilt.

The stutter can come back:

- After an update of this project that changes the shader compiler. The app then clears the game shader cache at the next launch. Run `./setup.sh` again: it skips the steps that are done and warms the cache again.
- If the warm-up was skipped (`--no-warm`) or did not finish. Run `./setup.sh` again.
- Possibly after a macOS update, if macOS rebuilds its Metal cache. This is not tested yet.

With caches filled by play, an 11-minute session had 0.09 late frames per second (see [Performance](#performance)).

## Saves

The game writes its saves and settings to its Windows profile folder. The app redirects that folder to `~/Library/Application Support/HarryPotterRecomp`. It never writes to the game folder.

The game's shader cache is in the same folder (see [Shader warm-up and stutter](#shader-warm-up-and-stutter)).

> [!WARNING]
> Back up `~/Library/Application Support/HarryPotterRecomp` before you play.

## Status

The menus, saves, the Year 1 opening, the Leaky Cauldron, Diagon Alley and Gringotts work, with sound. Testing so far: a 20-minute unattended run and several played sessions, without a crash.

- Short stutter the first time a new area, spell or effect appears, while its shaders compile. See [Shader warm-up and stutter](#shader-warm-up-and-stutter).
- Scene changes show a black screen for 1-5 seconds.
- Some cutscenes show faint trails behind moving objects.
- Controllers are not tested yet.

## Performance

Tested on a MacBook Pro 16-inch (2021) with an M1 Max (10-core CPU, 32-core GPU, 32 GB) and macOS 26.6. Borderless at 2056 × 1290 on a 120 Hz display.

| Scene | Result |
|---|---|
| Played by hand, 11 minutes, shader cache warm | 120 FPS; 0.09 late frames per second; no frame over 100 ms during play |

## Troubleshooting

| Problem | Fix |
|---|---|
| "game folder not found" | Set `HPRECOMP_GAME_DIR` to the folder that holds `LEGOHarryPotter.exe` |
| `setup.sh` says the sha256 does not match | Only build 660 works. Run SteamCMD's `app_update 21130 validate` again |
| macOS says the app is damaged | Build the app with `setup.sh` on your own Mac. A copy from another Mac does not run |
| Short stutter during play | Shaders compiling for the first time. It fades as you revisit areas. If it happens everywhere, the cache was cleared: run `./setup.sh` again. See [Shader warm-up and stutter](#shader-warm-up-and-stutter) |

## Building and development

[docs/BUILDING.md](docs/BUILDING.md) covers how the translation works, the Win32 layer, the dependency scripts in `tools/deps/`, building by hand, tests and the repository layout. The [MacRecomp README](https://github.com/itsdunintime104/MacRecomp#readme) covers the translator.

## Credits

- **[xboxrecomp](https://github.com/sp00nznet/xboxrecomp)** by sp00nznet: the x86-to-C translator MacRecomp's translator is forked from (MIT license).
- **[DXVK](https://github.com/doitsujin/dxvk)** (zlib license), with the macOS patches from [jm2/dxvk](https://github.com/jm2/dxvk): Direct3D 9 to Vulkan.
- **[vkd3d-shader](https://gitlab.winehq.org/wine/vkd3d)** (LGPL-2.1): compiles the game's HLSL shaders.
- **[Mesa](https://mesa3d.org) KosmicKrisp** (MIT license): Vulkan on Metal.
- **[FAudio](https://github.com/FNA-XNA/FAudio)** (zlib license): XAudio2.
- **[SDL](https://www.libsdl.org/)** (zlib license): window, input and controllers.
- **[Capstone](https://www.capstone-engine.org/)** (BSD license): x86 disassembly for the translator.
- **[Wine](https://www.winehq.org)**: used only as a reference to compare Win32 behaviour while developing, and its `widl` tool to build vkd3d. No Wine code is in the app.

## Support

[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-FFDD00?style=for-the-badge&logo=buy-me-a-coffee&logoColor=black)](https://buymeacoffee.com/itdunintime)

| Currency | Address |
|:---------|:--------|
| BTC | `1A7DmUpKRkuoSTKkWDcFR6SBdtpGqa1R8x` |
| ETH | `0x772a320ff05c763eae76868b971b6d53daee36e7` |
| SOL | `En858GF2YnTtTo4rmGfdTADof5gdVMGv1d13favtAbS5` |
| USDT (ERC-20) | `0x772a320ff05c763eae76868b971b6d53daee36e7` |

## License

GPL-3.0. See [LICENSE](LICENSE). Third-party code keeps its own license: see `engine/tools/xrecomp/LICENSE`, `engine/tools/xrecomp/NOTICE` and the header of `engine/runtime/src/dlmalloc.c`. The patches in `tools/` keep the license of the project they patch.

The repository contains no game assets, no game binary and no code generated from the game. `recon/` holds function addresses, import names and switch tables taken from the game binary, which the translator needs. The app icon is made from your own copy of the game when you build.
