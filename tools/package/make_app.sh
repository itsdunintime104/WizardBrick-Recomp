#!/bin/sh
# Build "Wizard Bricks Recompiled.app" from the native game binary: a launcher, the
# binary, and every non-system library it loads, with install names rewritten to
# @rpath so the app needs neither Homebrew nor DYLD_* variables at run time.
# Usage: make_app.sh <game binary> <output .app>
# Environment: DXVK_PREFIX, VKD3D_PREFIX, BREW_PREFIX (library sources);
# HP_GAME_DIR, HP_EXE (optional): the game folder and the game image (lhp_win32.exe)
# the launcher uses by default. The image is referenced by path, never bundled.
# The app icon comes from the first of the two that has one.
# KK_PREFIX (optional): install prefix of a patched KosmicKrisp build (see
# tools/package/patches/); its ICD manifest and library replace Homebrew's.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
[ $# -eq 2 ] && [ -x "$1" ] || { echo "make_app: usage: make_app.sh <game binary> <output .app>" >&2; exit 1; }
BIN=$1
EXE_DIR=$(dirname "$(realpath "$BIN")")
OUT_APP=$2
: "${DXVK_PREFIX:?make_app: set DXVK_PREFIX}" "${VKD3D_PREFIX:?make_app: set VKD3D_PREFIX}" "${BREW_PREFIX:?make_app: set BREW_PREFIX}"
APP_NAME=$(basename "$OUT_APP" .app)
APP_EXE=HarryPotterRecompiled   # must match CFBundleExecutable in Info.plist.in
GAME_ID=lhp
# The shell PATH may find a MacPorts x86_64 otool first.
OTOOL=/usr/bin/otool
INT=/usr/bin/install_name_tool

# Assemble and sign outside the repo: under ~/Documents the file provider adds
# Finder attributes to new folders at any moment, and codesign rejects them.
STAGE=$(mktemp -d "${TMPDIR:-/tmp}/$GAME_ID-app.XXXXXX")
trap 'rm -rf "$STAGE"' EXIT
APP="$STAGE/$APP_NAME.app"
FW="$APP/Contents/Frameworks"
ICD_DIR="$APP/Contents/Resources/vulkan/icd.d"
mkdir -p "$APP/Contents/MacOS" "$FW" "$ICD_DIR"
QUEUE="$STAGE/queue"   # one line per bundled Mach-O: <bundle path>|<source path>
: > "$QUEUE"

is_system() { case "$1" in /usr/lib/*|/System/*) return 0 ;; esac; return 1; }

# Copy one library into Frameworks under the name its users load it by.
add_lib() {
    [ -e "$FW/$2" ] && return
    [ -f "$1" ] || { echo "make_app: missing library $1" >&2; exit 1; }
    src=$(realpath "$1")
    cp "$src" "$FW/$2"
    chmod u+w "$FW/$2"
    echo "$FW/$2|$src" >> "$QUEUE"
}

# Find the file an @rpath/ or @loader_path/ reference in $2 (a source path) names.
# Rpaths may be relative to the loading file or to the game binary, as in a
# binary this script already packaged (@executable_path/../Frameworks).
resolve() {
    case "$1" in
        @rpath/*|@loader_path/*) ;;
        *) echo "$1"; return ;;
    esac
    leaf=${1##*/}
    for dir in "$(dirname "$2")" $($OTOOL -l "$2" | awk '$1 == "path" {print $2}'); do
        case "$dir" in
            @loader_path*) dir=$(dirname "$2")${dir#@loader_path} ;;
            @executable_path*) dir=$EXE_DIR${dir#@executable_path} ;;
        esac
        [ -f "$dir/$leaf" ] && { echo "$dir/$leaf"; return; }
    done
    echo "make_app: cannot resolve $1 (needed by $2)" >&2
    exit 1
}

# Point every non-system reference in one bundled Mach-O at @rpath, replace its
# rpaths with $3, and re-sign it: rewriting install names breaks the signature.
# Every library gets the rpath: dyld also searches it for dlopen of a leaf name
# (DXVK and SDL3 open libSDL2 and libvulkan that way). The game binary gets it
# only when it links a bundled library.
fix_macho() {
    f=$1; src=$2; rpath=$3
    id=$($OTOOL -D "$f" | tail -n +2)
    args=""
    [ -n "$id" ] && args="-id @rpath/${f##*/}"
    need_rpath=0
    [ -n "$id" ] && need_rpath=1
    for d in $($OTOOL -L "$f" | tail -n +2 | awk '{print $1}'); do
        [ "$d" = "$id" ] && continue
        is_system "$d" && continue
        leaf=${d##*/}
        # A reference resolve cannot follow stops the build unless the library is
        # already bundled from the prefixes above.
        lib=$(resolve "$d" "$src") || { [ -e "$FW/$leaf" ] || exit 1; lib=""; }
        add_lib "$lib" "$leaf"
        args="$args -change $d @rpath/$leaf"
        need_rpath=1
    done
    have=0
    for r in $($OTOOL -l "$f" | awk '$1 == "path" {print $2}'); do
        if [ "$r" = "$rpath" ]; then have=1; else args="$args -delete_rpath $r"; fi
    done
    [ "$need_rpath" -eq 1 ] && [ "$have" -eq 0 ] && args="$args -add_rpath $rpath"
    # shellcheck disable=SC2086  # these paths have no spaces
    [ -z "$args" ] || $INT $args "$f"
    codesign --force -s - "$f"
}

# The ICD manifest names its library by absolute path; the bundled copy names it
# relative to the manifest, which the Vulkan loader resolves from the manifest's folder.
ICD_JSON="${KK_PREFIX:-$BREW_PREFIX}/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json"
[ -f "$ICD_JSON" ] || { echo "make_app: KosmicKrisp ICD not found: $ICD_JSON (brew install mesa, or set KK_PREFIX)" >&2; exit 1; }
ICD_LIB=$(sed -n 's/.*"library_path": *"\([^"]*\)".*/\1/p' "$ICD_JSON")
sed "s|\"library_path\": *\"[^\"]*\"|\"library_path\": \"../../../Frameworks/${ICD_LIB##*/}\"|" \
    "$ICD_JSON" > "$ICD_DIR/kosmickrisp_mesa_icd.aarch64.json"

# Libraries loaded by name at run time, not linked: DXVK opens libSDL2-2.0.0.dylib
# and libvulkan.1.dylib, sdl2-compat opens libSDL3.dylib next to itself (a link to
# the libSDL3.0.dylib FAudio links, so one SDL3 loads), and the Vulkan loader opens
# the ICD. The game links DXVK, vkd3d-shader and FAudio.
add_lib "$DXVK_PREFIX/lib/libdxvk_d3d9.0.dylib" libdxvk_d3d9.0.dylib
add_lib "$VKD3D_PREFIX/lib/libvkd3d-shader.1.dylib" libvkd3d-shader.1.dylib
add_lib "$BREW_PREFIX/opt/faudio/lib/libFAudio.0.dylib" libFAudio.0.dylib
add_lib "$BREW_PREFIX/opt/sdl2/lib/libSDL2-2.0.0.dylib" libSDL2-2.0.0.dylib
add_lib "$BREW_PREFIX/opt/sdl3/lib/libSDL3.0.dylib" libSDL3.0.dylib
ln -s libSDL3.0.dylib "$FW/libSDL3.dylib"
add_lib "$BREW_PREFIX/opt/vulkan-loader/lib/libvulkan.1.dylib" libvulkan.1.dylib
add_lib "$ICD_LIB" "${ICD_LIB##*/}"

cp "$BIN" "$APP/Contents/MacOS/$GAME_ID"
chmod u+w "$APP/Contents/MacOS/$GAME_ID"
fix_macho "$APP/Contents/MacOS/$GAME_ID" "$(realpath "$BIN")" @executable_path/../Frameworks
# add_lib appends to the queue while this loop reads it.
n=1
while line=$(sed -n "${n}p" "$QUEUE"); [ -n "$line" ]; do
    fix_macho "${line%%|*}" "${line#*|}" @loader_path
    n=$((n + 1))
done

sed -e "s|@APP_EXE@|$APP_EXE|g" -e "s|@APP_NAME@|$APP_NAME|g" \
    "$HERE/Info.plist.in" > "$APP/Contents/Info.plist"

# Icon: taken from the user's game exe into the build output (never committed):
# LEGOHarryPotter.exe in HP_GAME_DIR first, then HP_EXE.
ICON=""
for exe in "${HP_GAME_DIR:+$HP_GAME_DIR/LEGOHarryPotter.exe}" "${HP_EXE:-}"; do
    [ -n "$exe" ] && [ -f "$exe" ] || continue
    if python3 "$HERE/exe_icon.py" "$exe" "$APP/Contents/Resources/AppIcon.icns"; then ICON=$exe; break; fi
done
if [ -n "$ICON" ]; then
    plutil -insert CFBundleIconFile -string AppIcon "$APP/Contents/Info.plist"
else
    echo "make_app: warning: no icon in the game exe; building without an icon" >&2
fi

# The launcher defaults to HP_GAME_DIR and HP_EXE; each path is single-quote-escaped.
sq() { printf '%s' "$1" | sed "s/'/'\\\\''/g"; }
{
    echo '#!/bin/sh'
    echo "DEFAULT_GAME_DIR='$(sq "${HP_GAME_DIR:-}")'"
    echo "DEFAULT_EXE='$(sq "${HP_EXE:-}")'"
    sed -e "s|@APP_NAME@|$APP_NAME|g" -e "s|@GAME_ID@|$GAME_ID|g" "$HERE/launcher.sh.in"
} > "$APP/Contents/MacOS/$APP_EXE"
chmod +x "$APP/Contents/MacOS/$APP_EXE"

xattr -cr "$APP"
codesign --force --deep -s - "$APP"
rm -rf "$OUT_APP"
ditto "$APP" "$OUT_APP"
echo "built $OUT_APP"
