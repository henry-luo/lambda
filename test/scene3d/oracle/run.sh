#!/bin/sh
# Compare native standard materials with the Three.js r186 reference, case by case.
# Run from the repository root:
#   CHROME_HEADLESS_SHELL=/path/to/chrome-headless-shell sh test/scene3d/oracle/run.sh [minimum] [case ...]
# Scene3D needs graphics, so run unsandboxed. Output goes to temp/pbr_oracle/.
set -e
DIR=temp/pbr_oracle
MINIMUM=${1:-0.99}
[ $# -gt 0 ] && shift
CASES=${*:-direct point neutral aces maps}
mkdir -p "$DIR"
PBR_ORACLE_DIR=$DIR ./lambda.exe run test/scene3d/oracle/_dump.ls --no-log > /dev/null
export LAMBDA_HEADLESS_GLFW_WINDOW=1
for key in $CASES; do
    size=$(python3 -c "import json,sys;d=json.load(open(sys.argv[1]));print(d['width'],d['height'])" "$DIR/$key.json")
    width=${size% *}; height=${size#* }
    printf '{"name":"pbr %s","viewport":{"width":%s,"height":%s},"events":[{"type":"wait","ms":200},{"type":"render","file":"%s/%s-native.png"}]}' \
        "$key" "$width" "$height" "$DIR" "$key" > "$DIR/$key-events.json"
    printf '{"canvasRect":{"x":0,"y":0,"width":%s,"height":%s}}' "$width" "$height" > "$DIR/$key-manifest.json"
    PBR_CASE=$key ./lambda.exe view test/scene3d/pbr-spheres.ls --headless --no-log \
        --event-file "$DIR/$key-events.json" --event-result "$DIR/$key-result.json" > "$DIR/$key-native.log" 2>&1
done
node test/scene3d/oracle/capture.cjs "$DIR" $CASES
for key in $CASES; do
    echo "== $key"
    python3 test/webgl/compare_pixels.py "$DIR/$key-native.png" --reference "$DIR/$key-chromium.png" \
        --manifest "$DIR/$key-manifest.json" --minimum "$MINIMUM" > "$DIR/$key-compare.json" || status=1
    grep channel_samples "$DIR/$key-compare.json"
done
exit ${status:-0}
