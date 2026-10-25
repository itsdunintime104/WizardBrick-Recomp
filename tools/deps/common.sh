# Shared setup for tools/deps/build_*.sh. Source it; do not run it.
# Sets: DEPS_ROOT (repo root), BREW, JOBS, SDK, and a clean arm64 PATH.
set -euo pipefail

DEPS_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
BREW=${BREW_PREFIX:-/opt/homebrew}
JOBS=${JOBS:-$(sysctl -n hw.ncpu)}

[ "$(uname -m)" = arm64 ] || { echo "error: these builds need an arm64 Mac" >&2; exit 1; }
# Only arm64 tools: an x86_64 tool early in PATH (for example a MacPorts
# otool or perl, or a /usr/local gcc) makes xcrun and configure fail.
export PATH="$BREW/bin:/usr/bin:/bin:/usr/sbin:/sbin"
SDK=$(xcrun --show-sdk-path)

die() { echo "error: $*" >&2; exit 1; }

# need_brew PKG...: stop with the list of missing Homebrew packages.
need_brew() {
    local p missing=()
    for p in "$@"; do [ -e "$BREW/opt/$p" ] || missing+=("$p"); done
    [ ${#missing[@]} -eq 0 ] || die "missing Homebrew packages: ${missing[*]} (brew install ${missing[*]})"
}

# abs_dir DIR: create DIR and print its absolute path.
abs_dir() { mkdir -p "$1" && (cd "$1" && pwd); }

# git_shallow DIR URL COMMIT: fetch only COMMIT into DIR (skip if DIR exists).
git_shallow() {
    local dir=$1 url=$2 commit=$3
    [ -d "$dir/.git" ] && return 0
    git init -q "$dir"
    git -C "$dir" fetch -q --depth 1 "$url" "$commit"
    git -C "$dir" checkout -q --detach FETCH_HEAD
    [ "$(git -C "$dir" rev-parse HEAD)" = "$commit" ] || die "$dir is not at $commit"
}

# fetch_sha256 URL FILE SHA256: download URL to FILE once and check its hash.
fetch_sha256() {
    local url=$1 file=$2 sum=$3
    if [ ! -f "$file" ]; then
        curl -fL --retry 3 -o "$file.part" "$url"
        mv "$file.part" "$file"
    fi
    echo "$sum  $file" | shasum -a 256 -c - >/dev/null || die "sha256 mismatch for $file"
}
