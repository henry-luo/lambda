#!/bin/sh
# State-dump parity harness — the migration oracle for the DOM-state work
# (vibe/Lambda_Design_DOM_State.md §8).
#
# Runs one UI-automation scenario twice over the same page: once with the Lambda
# dom package disabled, so Radiant's native default actions run, and once with it
# enabled, so the package's behavior templates run instead. Both passes emit the
# per-cascade Mark state-store dump, and the two dumps must match.
#
# Matching dumps mean the migrated behavior is state-equivalent to the native
# behavior it replaces — which is the property every F-phase flip has to hold.
#
# Usage: test/state_parity.sh <page> <event-json>
#   e.g. test/state_parity.sh test/lambda/ui/dom_pkg_checkbox.ls \
#                             test/ui/dom_pkg_checkbox.json

set -e
PAGE="$1"
EVENTS="$2"
if [ -z "$PAGE" ] || [ -z "$EVENTS" ]; then
    echo "usage: $0 <page> <event-json>" >&2
    exit 2
fi

OUT=temp/state_parity
rm -rf "$OUT" temp/state
mkdir -p "$OUT" temp/state

run_pass() {
    # $1 = RADIANT_DOM_PKG value, $2 = output name
    rm -f temp/state/*.mark
    RADIANT_DOM_PKG="$1" ./lambda.exe view "$PAGE" --event-file "$EVENTS" \
        --state-dump >"$OUT/$2.log" 2>&1 || true
    if ! ls temp/state/*.mark >/dev/null 2>&1; then
        echo "FAIL: pass '$2' produced no state dump (see $OUT/$2.log)" >&2
        exit 1
    fi
    # pids and monotonic sequence numbers differ per run by construction, so
    # normalize long integers before comparing; state values are short.
    sed -E 's/[0-9]{4,}/N/g' temp/state/*.mark > "$OUT/$2.mark"
}

run_pass 0 native
run_pass 1 package

if diff -u "$OUT/native.mark" "$OUT/package.mark" > "$OUT/parity.diff"; then
    echo "PARITY OK: $PAGE — package path is state-equivalent to native"
    exit 0
fi
echo "PARITY FAILED: $PAGE — see $OUT/parity.diff" >&2
head -40 "$OUT/parity.diff" >&2
exit 1
