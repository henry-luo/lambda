#!/usr/bin/env bash
set -euo pipefail

fixture_dir=$(mktemp -d ./temp/verify_loc_fixture.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT

mkdir -p "$fixture_dir/radiant"
cp utils/verify_loc_reduction.sh "$fixture_dir/verify_loc_reduction.sh"
cp utils/verify_loc_reduction_fixture_old.cpp "$fixture_dir/radiant/a.cpp"
cp utils/verify_loc_reduction_fixture_new.cpp "$fixture_dir/new_a.cpp"

git -C "$fixture_dir" init -q
git -C "$fixture_dir" config user.email fixture@example.test
git -C "$fixture_dir" config user.name fixture
git -C "$fixture_dir" add radiant/a.cpp
git -C "$fixture_dir" commit -qm baseline
cp "$fixture_dir/new_a.cpp" "$fixture_dir/radiant/a.cpp"

(cd "$fixture_dir" && ./verify_loc_reduction.sh --ref HEAD --min-reduction 2 \
    --files0-from <(printf 'radiant/a.cpp\0'))
