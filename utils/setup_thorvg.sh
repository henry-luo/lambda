#!/bin/bash

# the C API has Lottie stubs even without the loader; require its implementation.
thorvg_archive_has_lottie() {
    [ -f "$1" ] || return 1
    nm "$1" 2>/dev/null | awk '
        NF >= 3 && $(NF - 1) ~ /^[TW]$/ && $NF ~ /LottieLoader/ { found = 1 }
        END { exit !found }
    '
}
