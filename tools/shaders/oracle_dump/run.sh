#!/bin/sh
# Run the original game under Wine with the proxy d3dx9_41.dll, unattended.
# Usage: run.sh <oracle root> <seconds> [game args...]
# <oracle root> holds game/ (APFS clone), prefix/, dll/ (build.sh output), out/.
# The dump is copied to <oracle root>/out/dump and the Wine log to out/wine.log.
set -eu
root=$1
secs=$2
shift 2
wine_bin="$HOME/wine-11.3/Wine Staging.app/Contents/Resources/wine/bin"
export WINEPREFIX="$root/prefix"
export WINEDEBUG="${WINEDEBUG:--all,err+module,err+d3d}"
# No Mono/Gecko install prompts, no menu entries.
export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
if [ ! -f "$WINEPREFIX/system.reg" ]; then
    "$wine_bin/wineboot" -i
    "$wine_bin/wine" reg add 'HKCU\Software\Wine\Drivers' /v Audio /d '' /f
    "$wine_bin/wineserver" -w
fi
cp "$root/dll/d3dx9_41.dll" "$root/dll/d3dx9ms.dll" "$root/game/"
dump="$WINEPREFIX/drive_c/shaderdump"
rm -rf "$dump"
# The game caches compiled pairs under AppData; a warm cache means no compiles.
cache=$(find "$WINEPREFIX/drive_c/users" -type d -name CachedShaders 2>/dev/null | head -1)
[ -n "$cache" ] && rm -rf "$cache"
mkdir -p "$root/out"
export WINEDLLOVERRIDES="$WINEDLLOVERRIDES;d3dx9_41=n;d3dx9ms=n"
cd "$root/game"
"$wine_bin/wine" LEGOHarryPotter.exe -Windowed "$@" > "$root/out/wine.log" 2>&1 &
sleep "$secs"
"$wine_bin/wineserver" -k || true
"$wine_bin/wineserver" -w || true
rm -rf "$root/out/dump"
[ -d "$dump" ] && cp -R "$dump" "$root/out/dump"
rm -rf "$root/out/cache"
cache=$(find "$WINEPREFIX/drive_c/users" -type d -name CachedShaders 2>/dev/null | head -1)
[ -n "$cache" ] && cp -R "$cache" "$root/out/cache"
ls "$root/out/dump" 2>/dev/null | wc -l
