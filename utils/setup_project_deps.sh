#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INPUT_ROOT="${LAMBDA_INPUT_ROOT:-$ROOT/../lambda-input}"
RADIANT_ROOT="${RADIANT_ROOT:-$ROOT/../radiant}"
platform="${1:?platform required}"
case "${2:-}" in --dry-run) export SETUP_DRY_RUN=1 ;; '') ;; *) echo "Usage: setup-${platform}-deps.sh [--dry-run]" >&2; exit 2 ;; esac
source "$INPUT_ROOT/scripts/setup_packages.sh"
case "$platform" in
    mac) setup_packages mac git node premake pkg-config ccache coreutils googletest ;;
    linux)
        if [ "${SETUP_DRY_RUN:-0}" = 1 ]; then echo 'sudo apt update'; else sudo apt update; fi
        setup_packages linux build-essential clang git curl wget python3 python3-pip pkg-config ccache xxd nodejs npm libgtest-dev libgmock-dev libncurses5-dev coreutils uuid-dev ;;
    windows)
        case "${MSYSTEM:-}" in CLANG64) compiler=@toolchain@-clang ;; MINGW64) compiler=@toolchain@-gcc ;; *) echo 'Use an MSYS2 CLANG64 or MINGW64 shell.' >&2; exit 1 ;; esac
        setup_packages windows make "$compiler" git curl wget python @toolchain@-pkgconf @toolchain@-premake @toolchain@-nodejs @toolchain@-gtest ccache unzip vim ;;
    *) echo "Unknown setup platform: $platform" >&2; exit 2 ;;
esac
if [ "$platform" = linux ] && ! command -v premake5 >/dev/null 2>&1; then
    # Premake's own Make bootstrap is required when the distribution has no package.
    if [ "${SETUP_DRY_RUN:-0}" = 1 ]; then echo 'bootstrap premake5 with Bootstrap.mak'; else
        source_dir="$ROOT/temp/setup/premake-core"
        mkdir -p "$ROOT/temp/setup"
        [ -d "$source_dir/.git" ] || git clone --recurse-submodules https://github.com/premake/premake-core.git "$source_dir"
        make -C "$source_dir" -f Bootstrap.mak linux
        sudo install -m 755 "$source_dir/bin/release/premake5" /usr/local/bin/premake5
    fi
fi
if [ "${SETUP_DRY_RUN:-0}" = 1 ]; then
    echo "npm install --ignore-scripts (in $ROOT)"
    bash "$INPUT_ROOT/scripts/setup_deps.sh" "$platform" --dry-run
    bash "$RADIANT_ROOT/scripts/setup_deps.sh" "$platform" --skip-input --dry-run
else
    (cd "$ROOT" && PUPPETEER_SKIP_DOWNLOAD=true npm install --ignore-scripts)
    bash "$INPUT_ROOT/scripts/setup_deps.sh" "$platform"
    bash "$RADIANT_ROOT/scripts/setup_deps.sh" "$platform" --skip-input
fi
