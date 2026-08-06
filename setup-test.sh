#!/bin/sh

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REF_DIR="$ROOT_DIR/ref"
WPT_DIR="$REF_DIR/wpt"
WPT_URL="https://github.com/web-platform-tests/wpt.git"
WPT_COMMIT="${WPT_COMMIT:-4ea2132b9a14ad4baf33677ad1723789dd183d6b}"
LAMBDA_TEST_DIR="${LAMBDA_TEST_DIR:-$ROOT_DIR/../lambda-test}"

die() {
    echo "setup-test.sh: $*" >&2
    exit 1
}

command -v git >/dev/null 2>&1 || die "git is required"
[ -d "$LAMBDA_TEST_DIR" ] || die "lambda-test project not found: $LAMBDA_TEST_DIR"

mkdir -p "$REF_DIR"

if [ -e "$WPT_DIR" ] && ! git -C "$WPT_DIR" rev-parse --git-dir >/dev/null 2>&1; then
    if find "$WPT_DIR" -mindepth 1 -print -quit | grep -q .; then
        die "ref/wpt exists but is not a Git checkout: $WPT_DIR"
    fi
    rmdir "$WPT_DIR"
fi

if [ ! -e "$WPT_DIR" ]; then
    echo "Cloning WPT into $WPT_DIR"
    git clone --filter=blob:none --no-checkout "$WPT_URL" "$WPT_DIR"
fi

if ! git -C "$WPT_DIR" cat-file -e "$WPT_COMMIT^{commit}" 2>/dev/null; then
    echo "Fetching WPT commit $WPT_COMMIT"
    git -C "$WPT_DIR" fetch --filter=blob:none --depth=1 "$WPT_URL" "$WPT_COMMIT"
fi

current_wpt_commit=$(git -C "$WPT_DIR" rev-parse HEAD 2>/dev/null || true)
if [ "$current_wpt_commit" != "$WPT_COMMIT" ]; then
    if ! git -C "$WPT_DIR" diff --quiet || ! git -C "$WPT_DIR" diff --cached --quiet; then
        die "ref/wpt has local changes; refusing to switch commits"
    fi
    echo "Checking out WPT commit $WPT_COMMIT"
    git -C "$WPT_DIR" checkout --detach "$WPT_COMMIT"
fi

actual_wpt_commit=$(git -C "$WPT_DIR" rev-parse HEAD)
[ "$actual_wpt_commit" = "$WPT_COMMIT" ] || die "WPT checkout verification failed"
echo "WPT ready at $actual_wpt_commit"

for corpus_dir in "$LAMBDA_TEST_DIR"/*; do
    [ -d "$corpus_dir" ] || continue

    corpus_name=$(basename "$corpus_dir")
    [ "$corpus_name" = ".git" ] && continue

    link_path="$ROOT_DIR/test/$corpus_name"
    expected_target="../../lambda-test/$corpus_name"

    if [ -L "$link_path" ]; then
        existing_target=$(readlink "$link_path")
        if [ "$existing_target" = "$expected_target" ]; then
            echo "Link exists: test/$corpus_name"
        else
            die "conflicting symlink: test/$corpus_name -> $existing_target"
        fi
    elif [ -e "$link_path" ]; then
        echo "Path exists, leaving unchanged: test/$corpus_name"
    else
        ln -s "$expected_target" "$link_path"
        echo "Added link: test/$corpus_name -> $expected_target"
    fi
done
