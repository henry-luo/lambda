#!/bin/bash
# Parse test results and format a notification message.
# Usage: source notify.sh; format_notification <platform> <test_exit_code> <test_output_file>
# Output is written to $NOTIFICATION_MSG

format_notification() {
    local PLATFORM="$1"
    local EXIT_CODE="$2"
    local OUTPUT_FILE="$3"

    local STATUS="PASS ✅"
    if [ "$EXIT_CODE" -ne 0 ]; then
        STATUS="FAIL ❌"
    fi

    local PASSED=$(grep -c '\[  PASSED  \]' "$OUTPUT_FILE" 2>/dev/null || echo "0")
    local FAILED=$(grep -c '\[  FAILED  \]' "$OUTPUT_FILE" 2>/dev/null || echo "0")
    local TOTAL=$((PASSED + FAILED))

    local FAILED_TESTS=""
    if [ "$FAILED" -gt 0 ]; then
        FAILED_TESTS=$(grep '\[  FAILED  \]' "$OUTPUT_FILE" | head -20)
    fi

    NOTIFICATION_MSG="Lambda Nightly — ${PLATFORM}
Status: ${STATUS}
Date: $(date -u '+%Y-%m-%d %H:%M UTC')
Tests: ${PASSED}/${TOTAL} passed"

    if [ "$FAILED" -gt 0 ]; then
        NOTIFICATION_MSG="${NOTIFICATION_MSG}
Failed (${FAILED}):
${FAILED_TESTS}"
    fi
}
