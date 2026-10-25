#!/bin/sh
# Build the proxy d3dx9_41.dll. Usage: build.sh <real d3dx9_41.dll> <out dir>
# Output: <out dir>/d3dx9_41.dll (proxy) and <out dir>/d3dx9ms.dll (real copy).
set -eu
real=$1
out=$2
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"
python3 -I "$here/gen_def.py" "$real" "$out/proxy.def"
i686-w64-mingw32-gcc -O2 -Wall -shared -static-libgcc -o "$out/d3dx9_41.dll" \
    "$here/proxy.c" "$out/proxy.def" -Wl,--enable-stdcall-fixup
cp "$real" "$out/d3dx9ms.dll"
i686-w64-mingw32-gcc -O2 -Wall -static-libgcc -o "$out/compile_ms.exe" "$here/compile_ms.c"
