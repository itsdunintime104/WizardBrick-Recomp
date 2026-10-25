#!/bin/sh
# One-command setup: checks the machine, finds your own Windows copy of
# LEGO Harry Potter: Years 1-4, translates it, builds the native program and
# installs "Wizard Bricks Recompiled.app".
# Usage: ./setup.sh [--check] [--force] [--no-install] [--no-warm] [--dry-run] [--help]
set -eu

ROOT=$(cd "$(dirname "$0")" && pwd)
cd "$ROOT"

APP_NAME="Wizard Bricks Recompiled"
# The launcher script inside the app (tools/package/make_app.sh, APP_EXE).
LAUNCHER=HarryPotterRecompiled
GAME_EXE=LEGOHarryPotter.exe
BINARY_SHA256=21c2d30c07e3ac686df7b08462cc57517f599f8ddb9393ba35c01a5b7f9b16e3
ORIG="$ROOT/orig.nosync/lhp_win32.exe"
GEN="$ROOT/generated.nosync"
VENV="$ROOT/.venv"
# The engine (lifter and generic runtime): the engine/ submodule.
ENGINE="$ROOT/engine"
# The graphics libraries tools/deps builds: DXVK, vkd3d-shader, patched KosmicKrisp.
DEPS=${HPRECOMP_DEPS_DIR:-$ROOT/build.nosync/deps}
DXVK_PREFIX="$DEPS/dxvk"
VKD3D_PREFIX="$DEPS/vkd3d"
KK_PREFIX="$DEPS/kosmickrisp"

CHECK_ONLY=0
FORCE=0
INSTALL=1
WARM=1
DRY_RUN=0

usage() {
    cat <<USAGE
Usage: ./setup.sh [options]

Builds the app from your own Steam copy of the Windows game.

Options:
  --check       Check the requirements only, then stop.
  --force       Translate again even if generated.nosync is complete.
  --no-install  Build the app but do not copy it to ~/Applications.
  --no-warm     Do not build the game's shader cache after the build.
  --dry-run     Print each step and its commands without running them.
  --help        Show this text.

Environment:
  HPRECOMP_GAME_DIR  the folder that holds $GAME_EXE and GAME*.DAT,
                     if it is not in one of the folders setup.sh searches.
  HPRECOMP_DEPS_DIR  where tools/deps builds and installs the graphics
                     libraries (default: build.nosync/deps).
USAGE
}

for arg in "$@"; do
    case "$arg" in
        --check) CHECK_ONLY=1 ;;
        --force) FORCE=1 ;;
        --no-install) INSTALL=0 ;;
        --no-warm) WARM=0 ;;
        --dry-run) DRY_RUN=1 ;;
        --help|-h) usage; exit 0 ;;
        *) echo "setup: unknown option: $arg" >&2; usage >&2; exit 2 ;;
    esac
done

step() { echo "==> $*"; }
fail() { echo "setup: $*" >&2; exit 1; }
# Print a command; run it unless this is a dry run.
run() {
    printf '+'
    for a in "$@"; do
        case "$a" in *" "*) printf " '%s'" "$a" ;; *) printf ' %s' "$a" ;; esac
    done
    echo
    [ "$DRY_RUN" -eq 1 ] || "$@"
}
# Long lift and build commands share the job build lock when HPRECOMP_BUILD_LOCK is set.
run_locked() {
    if [ -n "${HPRECOMP_BUILD_LOCK:-}" ]; then
        run /usr/bin/lockf -k "$HPRECOMP_BUILD_LOCK" "$@"
    else
        run "$@"
    fi
}

MISSING=0
ok() { printf 'OK       %s\n' "$1"; }
missing() { printf 'MISSING  %s\n         fix: %s\n' "$1" "$2"; MISSING=$((MISSING + 1)); }
need_file() { if [ -e "$2" ]; then ok "$1 ($2)"; else missing "$1: $2" "$3"; fi; }

# The Homebrew packages tools/deps/build_$1.sh needs (its need_brew line).
dep_pkgs() { sed -n 's/^need_brew //p' "$ROOT/tools/deps/build_$1.sh"; }

# The graphics libraries not built yet, and the Homebrew packages that build them.
check_deps() {
    todo=""
    [ -f "$DXVK_PREFIX/lib/libdxvk_d3d9.0.dylib" ] || todo="$todo dxvk"
    [ -f "$VKD3D_PREFIX/lib/libvkd3d-shader.1.dylib" ] || todo="$todo vkd3d"
    [ -f "$KK_PREFIX/lib/libvulkan_kosmickrisp.dylib" ] || todo="$todo kosmickrisp"
    if [ -z "$todo" ]; then
        ok "DXVK, vkd3d-shader, KosmicKrisp ($DEPS)"
        return
    fi
    printf 'TO BUILD %s (step 5, into %s)\n' "${todo# }" "$DEPS"
    absent=""
    for p in $(for d in $todo; do dep_pkgs "$d"; done | tr ' ' '\n' | sort -u); do
        [ -e "$BREW_PREFIX/opt/$p" ] || absent="$absent $p"
    done
    if [ -n "$absent" ]; then
        missing "Homebrew packages to build them:$absent" "brew install$absent"
    else
        ok "Homebrew packages to build them"
    fi
    case "$todo" in *kosmickrisp*)
        want=$(sed -n 's/^LLVM_MAJOR=//p' "$ROOT/tools/deps/build_kosmickrisp.sh")
        have=$("$BREW_PREFIX/opt/llvm/bin/llvm-config" --version 2>/dev/null | cut -d. -f1) || have=""
        if [ "$have" = "$want" ]; then ok "LLVM $want"; else missing "LLVM $want (Homebrew llvm: ${have:-not installed})" "brew install llvm (KosmicKrisp needs version $want)"; fi
    esac
}

# Print the first python3 on PATH that is 3.11 or newer (the default python3 may be older).
find_python() {
    for cand in python3 python3.14 python3.13 python3.12 python3.11; do
        if command -v "$cand" >/dev/null 2>&1 \
            && "$cand" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 11) else 1)' 2>/dev/null; then
            command -v "$cand"
            return
        fi
    done
}

# Run one build tool with a harmless command and report it.
# Usage: check_tool NAME FIX COMMAND...
# A tool that is found but fails is often another copy earlier in PATH
# (for example MacPorts); Apple's copy is in /usr/bin.
check_tool() {
    name=$1; fix=$2; shift 2
    path=$(command -v "$1" 2>/dev/null) || { missing "$name: not found" "$fix"; return; }
    if err=$("$@" 2>&1 >/dev/null); then
        ok "$name ($path)"
        return
    fi
    case "$path" in
        /usr/bin/*) ;;
        *) fix="$path is not Apple's copy. Put Apple's tools first: PATH=/usr/bin:\$PATH ./setup.sh" ;;
    esac
    missing "$name: $path fails: $(printf '%s\n' "$err" | head -n 1 | cut -c 1-120)" "$fix"
}

check_build_tools() {
    T=$(mktemp -d "${TMPDIR:-/tmp}/recomp-setup.XXXXXX")
    trap 'rm -rf "$T"' EXIT INT TERM
    printf 'int main(void) { return 0; }\n' > "$T/t.c"
    check_tool "C compiler (clang, arm64)" "xcode-select --install" clang -arch arm64 "$T/t.c" -o "$T/t"
    check_tool "make" "xcode-select --install" make --version
    check_tool "otool" "xcode-select --install" /usr/bin/otool -h /usr/bin/true
    if [ -f "$T/t" ]; then
        check_tool "install_name_tool" "xcode-select --install" /usr/bin/install_name_tool -add_rpath @loader_path "$T/t"
        check_tool "codesign" "xcode-select --install" codesign --force -s - "$T/t"
    else
        printf 'SKIPPED  install_name_tool, codesign (need the C compiler)\n'
    fi
    rm -rf "$T"
    trap - EXIT INT TERM
}

preflight() {
    step "1/9 Checking requirements"

    if [ "$(uname -s)" = Darwin ]; then ok "macOS"; else missing "macOS (this is $(uname -s))" "run this on a Mac"; fi
    if [ "$(uname -m)" = arm64 ]; then ok "Apple Silicon (arm64)"; else missing "Apple Silicon (this is $(uname -m))" "use a Mac with an M-series chip"; fi

    if xcode-select -p >/dev/null 2>&1; then
        ok "Xcode Command Line Tools"
        check_build_tools
    else
        missing "Xcode Command Line Tools (C compiler, make, otool)" "xcode-select --install"
    fi

    BREW_PREFIX=""
    if command -v brew >/dev/null 2>&1; then
        BREW_PREFIX=$(brew --prefix)
        # tools/deps reads it too.
        export BREW_PREFIX
        ok "Homebrew ($BREW_PREFIX)"
        # "brew --prefix <formula>" can print an alias path without lib/, so build paths from the Homebrew prefix.
        need_file "SDL2 (sdl2-compat)" "$BREW_PREFIX/opt/sdl2/lib/libSDL2-2.0.0.dylib" "brew install sdl2"
        need_file "SDL3" "$BREW_PREFIX/opt/sdl3/lib/libSDL3.0.dylib" "brew install sdl3"
        need_file "FAudio" "$BREW_PREFIX/opt/faudio/lib/libFAudio.0.dylib" "brew install faudio"
        need_file "Vulkan loader" "$BREW_PREFIX/opt/vulkan-loader/lib/libvulkan.1.dylib" "brew install vulkan-loader"
        check_deps
    else
        missing "Homebrew (needed for SDL2, SDL3, FAudio, Vulkan and the graphics library builds)" \
            "install Homebrew from https://brew.sh, then run ./setup.sh --check again"
    fi
    need_file "engine (engine/ submodule)" "$ENGINE/tools/xrecomp/run.py" "git submodule update --init"

    PYTHON=$(find_python)
    if [ -n "$PYTHON" ]; then ok "Python 3.11 or newer ($PYTHON)"; else missing "Python 3.11 or newer" "brew install python"; fi

    FREE_KB=$(df -k "$ROOT" | awk 'NR==2 {print $4}')
    if [ "${FREE_KB:-0}" -ge 4194304 ]; then
        ok "Free disk space (at least 4 GB)"
    else
        missing "Free disk space (at least 4 GB)" "free up disk space"
    fi

    if [ "$MISSING" -gt 0 ]; then
        echo "setup: $MISSING requirement(s) missing. Run the fix commands above, then run ./setup.sh again." >&2
        # A dry run shows the remaining steps anyway.
        [ "$DRY_RUN" -eq 1 ] || exit 1
    fi
}

# Print the first folder that holds the game: HPRECOMP_GAME_DIR, then Steam
# under Wine, CrossOver or Whisky, then a Mac Steam library with the Windows files.
find_game_dir() {
    STEAM_REL="Program Files (x86)/Steam/steamapps/common/LEGO Harry Potter"
    for dir in "${HPRECOMP_GAME_DIR:-}" \
        "$HOME"/.wine*/drive_c/"$STEAM_REL" \
        "$HOME/Library/Application Support/CrossOver/Bottles"/*/drive_c/"$STEAM_REL" \
        "$HOME/Library/Containers/com.isaacmarovitz.Whisky/Bottles"/*/drive_c/"$STEAM_REL" \
        "$HOME/Library/Application Support/Steam/steamapps/common/LEGO Harry Potter"; do
        [ -n "$dir" ] && [ -f "$dir/$GAME_EXE" ] && { echo "$dir"; return; }
    done
}

locate_and_copy_game() {
    step "2/9 Locating the game"
    GAME_DIR=$(find_game_dir)
    if [ -z "$GAME_DIR" ]; then
        msg="$GAME_EXE not found. Install LEGO Harry Potter: Years 1-4 from Steam (Windows version),
or set HPRECOMP_GAME_DIR to the folder that holds $GAME_EXE."
        [ "$DRY_RUN" -eq 1 ] || fail "$msg"
        echo "setup: $msg" >&2
        GAME_DIR='<game folder>'
    else
        echo "found $GAME_DIR/$GAME_EXE"
        ls "$GAME_DIR"/GAME*.DAT >/dev/null 2>&1 || fail "no GAME*.DAT files next to $GAME_EXE; the install is incomplete"
    fi

    step "3/9 Copying and verifying the game binary"
    if [ -f "$ORIG" ] && cmp -s "$GAME_DIR/$GAME_EXE" "$ORIG"; then
        echo "$ORIG is up to date"
    else
        run mkdir -p "$(dirname "$ORIG")"
        run cp "$GAME_DIR/$GAME_EXE" "$ORIG"
    fi
    echo "+ shasum -a 256 $ORIG"
    [ "$DRY_RUN" -eq 1 ] && return
    SHA=$(shasum -a 256 "$ORIG" | awk '{print $1}')
    [ "$SHA" = "$BINARY_SHA256" ] || fail "the game binary does not match (sha256 $SHA).
Only the current Steam build of $GAME_EXE is supported. In Steam, check that the game is up to date and not on a beta branch."
    echo "sha256 matches"
}

setup_venv() {
    step "4/9 Python environment"
    if [ -x "$VENV/bin/python" ] && "$VENV/bin/python" -c 'import capstone' >/dev/null 2>&1; then
        echo ".venv is ready"
    else
        run "$PYTHON" -m venv "$VENV"
        run "$VENV/bin/python" -m pip install --quiet capstone
    fi
}

# Each tools/deps script skips the steps whose output already exists.
build_deps() {
    step "5/9 Building the graphics libraries (the first run takes several minutes)"
    for d in dxvk vkd3d kosmickrisp; do
        run_locked "$ROOT/tools/deps/build_$d.sh" "$DEPS/$d-work" "$DEPS/$d"
    done
}

translate() {
    step "6/9 Translating the game code"
    if [ -f "$GEN/lift_report.json" ] && [ "$FORCE" -eq 0 ]; then
        echo "generated.nosync is complete; skipping (use --force to translate again)"
        return
    fi
    # recon/ holds the committed function analysis of this exact binary.
    run_locked "$VENV/bin/python" -I "$ROOT/tools/pe/run_lift_pe.py" --engine "$ENGINE" \
        --analysis "$ROOT/recon/analysis.json" --switch "$ROOT/recon/switch_tables.json" \
        --bin "$ORIG" --out "$GEN" --prefix lhp --jobs "$NCPU"
}

build() {
    step "7/9 Building the game and the app"
    run_locked make -C "$ROOT/build" -j"$NCPU" ENGINE="$ENGINE" GEN="$GEN" DEPS="$DEPS" \
        BREW_PREFIX="$BREW_PREFIX" HP_GAME_DIR="$GAME_DIR" HP_EXE="$ORIG" app
}

install_app() {
    step "8/9 Installing"
    APP="$ROOT/build.nosync/$APP_NAME.app"
    [ "$DRY_RUN" -eq 1 ] || [ -d "$APP" ] || fail "build did not produce $APP"
    if [ "$INSTALL" -eq 1 ]; then
        DEST="$HOME/Applications/$APP_NAME.app"
        run mkdir -p "$HOME/Applications"
        run rm -rf "$DEST"
        # -X: leave out extended attributes. A build folder under ~/Documents
        # carries file-sync attributes that fail codesign --strict.
        run cp -RX "$APP" "$DEST"
        APP="$DEST"
    else
        echo "built $APP (not installed)"
    fi
}

# 0 when every CachedShaders in the save folder has the stamp of this
# compiler revision (runtime/src/d3dx_shader.c, d3dx_game_cache_stamp).
cache_is_current() {
    rev=$(sed -n 's/^#define D3DX_COMPILER_REVISION "\(.*\)"$/\1/p' "$ROOT/runtime/src/d3dx_shader.c")
    stamps=$(find "$SAVE/Users" -path '*/AppData/Roaming/*/*/CachedShaders/VERSION.DAT' 2>/dev/null) || stamps=""
    [ -n "$rev" ] && [ -n "$stamps" ] || return 1
    printf '%s\n' "$stamps" | while IFS= read -r v; do
        [ "$(cat "$(dirname "$v")/RECOMP_D3DX.TXT" 2>/dev/null)" = "$rev" ] || exit 1
    done
}

# Runs the game's shader dump routine once over every scene
# (HPRECOMP_WARM_SHADERS, runtime/src/hp_boot.c), so the first play does not
# stop to compile shaders.
warm_cache() {
    step "9/9 Building the game's shader cache"
    SAVE="$HOME/Library/Application Support/HarryPotterRecomp"
    if [ "$WARM" -eq 0 ]; then
        echo "skipped (--no-warm)"
        return
    fi
    if cache_is_current; then
        echo "the shader cache is already built by this version; skipping"
        return
    fi
    FT=$(mktemp "${TMPDIR:-/tmp}/recomp-filetable.XXXXXX")
    run "$VENV/bin/python" -I "$ROOT/tools/shaders/scene_list.py" "$GAME_DIR" "$FT"
    echo "The game window opens for about 6 minutes while the game compiles its shaders."
    echo "Leave it open; it closes by itself."
    if ! run env HPRECOMP_WARM_SHADERS=1 HPRECOMP_WARM_FILETABLE="$FT" RECOMP_ASSUME_FOCUS=1 \
        "$APP/Contents/MacOS/$LAUNCHER"; then
        echo "setup: the shader warm-up did not finish. The game still works; it compiles shaders as you play." >&2
        echo "Its log is in $HOME/Library/Logs/HarryPotterRecomp/" >&2
    fi
    rm -f "$FT"
}

preflight
if [ "$CHECK_ONLY" -eq 1 ]; then
    [ "$MISSING" -eq 0 ] && echo "All requirements are met."
    exit 0
fi
NCPU=$(sysctl -n hw.ncpu)
locate_and_copy_game
setup_venv
build_deps
translate
build
install_app
warm_cache
echo
echo "Start it by double-clicking $APP, or run: open \"$APP\""
echo "Saves: $HOME/Library/Application Support/HarryPotterRecomp/"
echo "Logs: $HOME/Library/Logs/HarryPotterRecomp/"
