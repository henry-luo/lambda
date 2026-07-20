#!/bin/sh
set -eu

out_dir="temp/js_mir_profile"
mkdir -p "$out_dir"

if [ "$#" -eq 0 ]; then
    set -- \
        test/js/mir_tune_basics.js \
        test/js/mir_tune_objects_calls.js \
        test/js/mir_tune_control_exceptions.js \
        test/js/mir_tune_advanced.js
fi

for source in "$@"; do
    base=$(basename "$source" .js)
    rm -f temp/js_mir_dump.txt log.txt
    JS_MIR_DUMP=1 LAMBDA_MIR_LOG_FRAME_SLOTS=1 \
        ./lambda.exe js "$source" >"$out_dir/$base.run" 2>&1
    cp temp/js_mir_dump.txt "$out_dir/$base.mir"
    if [ -f log.txt ]; then
        cp log.txt "$out_dir/$base.log"
    else
        : >"$out_dir/$base.log"
    fi
done

python3 utils/analyze_js_mir.py --check "$out_dir"/*.mir
