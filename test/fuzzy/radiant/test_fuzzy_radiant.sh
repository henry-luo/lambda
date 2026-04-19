#!/bin/bash
# Radiant Layout Engine Fuzzy Test Runner
#
# Orchestrates generative + mutational fuzzing of the Radiant layout engine.
# Generates adversarial HTML/CSS, runs layout, and captures crashes/timeouts.
#
# Usage:
#   ./test/fuzzy/radiant/test_fuzzy_radiant.sh [OPTIONS]
#
# Options:
#   --duration=SECONDS     Total fuzzing duration (default: 300)
#   --timeout=SECONDS      Per-file layout timeout (default: 10)
#   --gen-count=N          Generated files per round (default: 50)
#   --mut-count=N          Mutations per seed file (default: 3)
#   --mut-seeds=N          Max seed files to mutate per round (default: 50)
#   --mode=MODE            Generator mode: flex,grid,table,block,float,text,
#                          inline,position,mixed,all (default: all)
#   --mutate-only          Only run mutation-based fuzzing
#   --generate-only        Only run generation-based fuzzing
#   --verbose|-v           Show individual test results
#   --seed=N               Random seed for reproducibility

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
LAMBDA_EXE="$PROJECT_ROOT/lambda.exe"
GENERATOR="$SCRIPT_DIR/generators/html_gen.py"
MUTATOR="$SCRIPT_DIR/generators/html_mutator.py"
SEED_DIR="$PROJECT_ROOT/test/layout/data"
TEMP_DIR="$PROJECT_ROOT/temp/radiant_fuzz"
CRASH_DIR="$SCRIPT_DIR/results/crashes"
TIMEOUT_DIR="$SCRIPT_DIR/results/timeouts"
SLOW_DIR="$SCRIPT_DIR/results/slow"
BADJSON_DIR="$SCRIPT_DIR/results/badjson"

# Defaults
DURATION_SEC=600
TIMEOUT_SEC=10
GEN_COUNT=100
MUT_COUNT=5
MUT_SEEDS=100
MODE="all"
MUTATE_ONLY=0
GENERATE_ONLY=0
VERBOSE=0
RANDOM_SEED=""
STRESS=0

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --duration=*)    DURATION_SEC="${1#*=}"; shift ;;
        --timeout=*)     TIMEOUT_SEC="${1#*=}"; shift ;;
        --gen-count=*)   GEN_COUNT="${1#*=}"; shift ;;
        --mut-count=*)   MUT_COUNT="${1#*=}"; shift ;;
        --mut-seeds=*)   MUT_SEEDS="${1#*=}"; shift ;;
        --mode=*)        MODE="${1#*=}"; shift ;;
        --mutate-only)   MUTATE_ONLY=1; shift ;;
        --generate-only) GENERATE_ONLY=1; shift ;;
        --verbose|-v)    VERBOSE=1; shift ;;
        --seed=*)        RANDOM_SEED="${1#*=}"; shift ;;
        --stress)        STRESS=1; shift ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --duration=SECONDS     Total fuzzing duration (default: 300)"
            echo "  --timeout=SECONDS      Per-file layout timeout (default: 10)"
            echo "  --gen-count=N          Generated files per round (default: 50)"
            echo "  --mut-count=N          Mutations per seed file (default: 3)"
            echo "  --mut-seeds=N          Max seeds to mutate per round (default: 50)"
            echo "  --mode=MODE            Generator mode (default: all)"
            echo "  --mutate-only          Only run mutation-based fuzzing"
            echo "  --generate-only        Only run generation-based fuzzing"
            echo "  --verbose|-v           Show individual test results"
            echo "  --seed=N               Random seed for reproducibility"
            echo "  --stress               Stress mode: 200 gen, 10 mut, 200 seeds, 5s timeout"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Apply stress preset (overridable by explicit args before --stress)
if [[ "$STRESS" -eq 1 ]]; then
    GEN_COUNT=200
    MUT_COUNT=10
    MUT_SEEDS=200
    TIMEOUT_SEC=5
fi

# Check prerequisites
if [[ ! -f "$LAMBDA_EXE" ]]; then
    echo -e "${RED}Error: lambda.exe not found at $LAMBDA_EXE${NC}"
    echo "Run 'make build' first"
    exit 1
fi

if ! command -v python3 &>/dev/null; then
    echo -e "${RED}Error: python3 not found${NC}"
    exit 1
fi

# Create directories
mkdir -p "$TEMP_DIR/gen" "$TEMP_DIR/mut" "$TEMP_DIR/out"
mkdir -p "$CRASH_DIR" "$TIMEOUT_DIR" "$SLOW_DIR" "$BADJSON_DIR"

# Marker file to identify timeout files from this session
RUN_MARKER="$TEMP_DIR/.run_marker"
touch "$RUN_MARKER"

# Retry timeout for slow-test detection (6x the normal timeout, minimum 30s)
RETRY_TIMEOUT=$((TIMEOUT_SEC * 6))
if [[ $RETRY_TIMEOUT -lt 30 ]]; then
    RETRY_TIMEOUT=30
fi

# Statistics
TESTS_RUN=0
TESTS_PASSED=0
TESTS_CRASHED=0
TESTS_TIMEOUT=0
TESTS_SLOW=0
TESTS_BADJSON=0
TESTS_ERROR=0
ROUND=0
START_TIME=$(date +%s)

log_verbose() {
    if [[ "$VERBOSE" -eq 1 ]]; then
        echo -e "$@"
    fi
}

# Run layout on a single HTML file
run_one() {
    local html_file="$1"
    local label="$2"
    local json_out="$TEMP_DIR/out/result_$$.json"

    TESTS_RUN=$((TESTS_RUN + 1))

    # Run with timeout (--no-log to avoid debug I/O overhead skewing results)
    local exit_code=0
    timeout "$TIMEOUT_SEC" "$LAMBDA_EXE" layout "$html_file" --no-log -o "$json_out" \
        >/dev/null 2>"$TEMP_DIR/out/stderr_$$.txt" || exit_code=$?

    local basename
    basename=$(basename "$html_file")

    case $exit_code in
        0)
            # Check output is valid JSON
            if [[ -f "$json_out" ]]; then
                if python3 -c "
import json, sys
try:
    json.load(open(sys.argv[1]))
except Exception:
    sys.exit(1)
" "$json_out" 2>/dev/null; then
                    TESTS_PASSED=$((TESTS_PASSED + 1))
                    log_verbose "  ${GREEN}PASS${NC}: $basename"
                else
                    TESTS_BADJSON=$((TESTS_BADJSON + 1))
                    local ts
                    ts=$(date +%Y%m%d_%H%M%S)
                    cp "$html_file" "$BADJSON_DIR/badjson_${ts}_${TESTS_BADJSON}.html"
                    echo -e "  ${YELLOW}BAD JSON${NC}: $basename"
                fi
            else
                # No output file but exit 0 - count as pass
                TESTS_PASSED=$((TESTS_PASSED + 1))
                log_verbose "  ${GREEN}PASS (no output)${NC}: $basename"
            fi
            ;;
        124|137)
            # Timeout: 124=SIGTERM from timeout(1), 137=SIGKILL from timeout(1)
            # when process doesn't exit after SIGTERM (e.g., stuck after JS timeout)
            TESTS_TIMEOUT=$((TESTS_TIMEOUT + 1))
            local ts
            ts=$(date +%Y%m%d_%H%M%S)
            cp "$html_file" "$TIMEOUT_DIR/timeout_${ts}_${TESTS_TIMEOUT}.html"
            echo -e "  ${YELLOW}TIMEOUT${NC}: $basename ($label)"
            ;;
        134|136|138|139)
            # SIGABRT=134, SIGFPE=136, SIGBUS=138, SIGSEGV=139
            TESTS_CRASHED=$((TESTS_CRASHED + 1))
            local ts
            ts=$(date +%Y%m%d_%H%M%S)
            local sig_name=""
            case $exit_code in
                134) sig_name="SIGABRT" ;;
                136) sig_name="SIGFPE" ;;
                139) sig_name="SIGSEGV" ;;
                138) sig_name="SIGBUS" ;;
                *) sig_name="SIG$exit_code" ;;
            esac
            cp "$html_file" "$CRASH_DIR/crash_${sig_name}_${ts}_${TESTS_CRASHED}.html"
            echo -e "  ${RED}CRASH ($sig_name)${NC}: $basename ($label)"
            # Save stderr for crash analysis
            if [[ -f "$TEMP_DIR/out/stderr_$$.txt" ]]; then
                cp "$TEMP_DIR/out/stderr_$$.txt" "$CRASH_DIR/crash_${sig_name}_${ts}_${TESTS_CRASHED}.stderr"
            fi
            ;;
        *)
            # Non-crash error (parse error, etc) - expected for fuzzy inputs
            TESTS_ERROR=$((TESTS_ERROR + 1))
            log_verbose "  ${GREEN}OK (error $exit_code)${NC}: $basename"
            ;;
    esac

    # Cleanup
    rm -f "$json_out" "$TEMP_DIR/out/stderr_$$.txt"
}

# Run a batch of HTML files
run_batch() {
    local dir="$1"
    local label="$2"

    for html_file in "$dir"/*.html; do
        [[ -f "$html_file" ]] || continue

        # Check time limit
        local now
        now=$(date +%s)
        local elapsed=$((now - START_TIME))
        if [[ $elapsed -ge $DURATION_SEC ]]; then
            return 1
        fi

        run_one "$html_file" "$label"
    done
    return 0
}

# Collect seed files for mutation
collect_seeds() {
    local seeds=()
    # Gather HTML files from test/layout/data subdirectories
    while IFS= read -r -d '' f; do
        seeds+=("$f")
    done < <(find "$SEED_DIR" -name '*.html' -size -100k -print0 2>/dev/null)

    # Shuffle and take MUT_SEEDS
    if [[ ${#seeds[@]} -gt 0 ]]; then
        printf '%s\n' "${seeds[@]}" | sort -R | head -n "$MUT_SEEDS"
    fi
}

# Print progress
print_progress() {
    local now
    now=$(date +%s)
    local elapsed=$((now - START_TIME))
    local remaining=$((DURATION_SEC - elapsed))
    if [[ $remaining -lt 0 ]]; then remaining=0; fi

    echo -e "${CYAN}[Round $ROUND | ${elapsed}s/${DURATION_SEC}s]${NC}" \
        "run=$TESTS_RUN pass=$TESTS_PASSED" \
        "${RED}crash=$TESTS_CRASHED${NC}" \
        "${YELLOW}timeout=$TESTS_TIMEOUT${NC}" \
        "slow=$TESTS_SLOW" \
        "badjson=$TESTS_BADJSON error=$TESTS_ERROR"
}

# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------

echo -e "${BOLD}╔══════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}║   Radiant Layout Engine — Fuzzy Test Runner      ║${NC}"
echo -e "${BOLD}╚══════════════════════════════════════════════════╝${NC}"
echo ""
echo "  Duration:     ${DURATION_SEC}s"
echo "  Timeout:      ${TIMEOUT_SEC}s per file"
echo "  Mode:         $MODE"
echo "  Gen/round:    $GEN_COUNT"
echo "  Mut/seed:     $MUT_COUNT"
echo "  Max seeds:    $MUT_SEEDS"
if [[ -n "$RANDOM_SEED" ]]; then
    echo "  Seed:         $RANDOM_SEED"
fi
echo ""

SEED_ARG=""
if [[ -n "$RANDOM_SEED" ]]; then
    SEED_ARG="--seed=$RANDOM_SEED"
fi

while true; do
    ROUND=$((ROUND + 1))

    # Check time limit
    local_now=$(date +%s)
    local_elapsed=$((local_now - START_TIME))
    if [[ $local_elapsed -ge $DURATION_SEC ]]; then
        break
    fi

    # Per-round seed (deterministic if global seed set)
    if [[ -n "$RANDOM_SEED" ]]; then
        ROUND_SEED=$((RANDOM_SEED + ROUND))
        ROUND_SEED_ARG="--seed=$ROUND_SEED"
    else
        ROUND_SEED_ARG=""
    fi

    # --- Tier 1: Generated HTML ---
    if [[ $MUTATE_ONLY -eq 0 ]]; then
        # Clean gen dir
        rm -f "$TEMP_DIR/gen/"*.html 2>/dev/null || true

        python3 "$GENERATOR" \
            --mode="$MODE" \
            --count="$GEN_COUNT" \
            --output-dir="$TEMP_DIR/gen" \
            $ROUND_SEED_ARG \
            2>/dev/null

        run_batch "$TEMP_DIR/gen" "generated/$MODE" || break
    fi

    # --- Tier 2: Mutated existing tests ---
    if [[ $GENERATE_ONLY -eq 0 ]]; then
        # Clean mut dir
        rm -f "$TEMP_DIR/mut/"*.html 2>/dev/null || true

        # Pick random seeds
        local_seeds=$(collect_seeds)
        if [[ -n "$local_seeds" ]]; then
            # Create a temp file listing seeds
            seed_list="$TEMP_DIR/seed_list.txt"
            echo "$local_seeds" > "$seed_list"

            # Mutate each seed
            while IFS= read -r seed_file; do
                [[ -f "$seed_file" ]] || continue
                python3 "$MUTATOR" \
                    "$seed_file" \
                    --count="$MUT_COUNT" \
                    --output-dir="$TEMP_DIR/mut" \
                    $ROUND_SEED_ARG \
                    2>/dev/null || true
            done < "$seed_list"

            run_batch "$TEMP_DIR/mut" "mutated" || break
        fi
    fi

    print_progress
done

# ---------------------------------------------------------------------------
# Retry timeouts with longer limit to distinguish slow from truly hanging
# ---------------------------------------------------------------------------
TIMEOUT_FILES=()
while IFS= read -r -d '' f; do
    TIMEOUT_FILES+=("$f")
done < <(find "$TIMEOUT_DIR" -name 'timeout_*.html' -newer "$RUN_MARKER" -print0 2>/dev/null)

if [[ ${#TIMEOUT_FILES[@]} -gt 0 ]]; then
    echo ""
    echo -e "${CYAN}Retrying ${#TIMEOUT_FILES[@]} timeout file(s) with ${RETRY_TIMEOUT}s limit...${NC}"
    for tf in "${TIMEOUT_FILES[@]}"; do
        local_basename=$(basename "$tf")
        local_size=$(wc -c < "$tf" | tr -d ' ')
        echo -n "  $local_basename (${local_size}B): "
        local_start=$(date +%s)
        local_rc=0
        timeout "$RETRY_TIMEOUT" "$LAMBDA_EXE" layout "$tf" --no-log \
            >/dev/null 2>/dev/null || local_rc=$?
        local_end=$(date +%s)
        local_elapsed=$((local_end - local_start))

        case $local_rc in
            0|1)
                # Completed (0=success, 1=handled error) — reclassify as slow
                TESTS_TIMEOUT=$((TESTS_TIMEOUT - 1))
                TESTS_SLOW=$((TESTS_SLOW + 1))
                local_ts=$(date +%Y%m%d_%H%M%S)
                mv "$tf" "$SLOW_DIR/slow_${local_ts}_${TESTS_SLOW}.html"
                echo -e "${GREEN}PASS${NC} in ${local_elapsed}s → reclassified as ${YELLOW}SLOW${NC}"
                ;;
            124|137)
                echo -e "${RED}TIMEOUT${NC} at ${RETRY_TIMEOUT}s (likely hanging)"
                ;;
            134|136|138|139)
                # Crashed on retry — reclassify
                TESTS_TIMEOUT=$((TESTS_TIMEOUT - 1))
                TESTS_CRASHED=$((TESTS_CRASHED + 1))
                local_ts=$(date +%Y%m%d_%H%M%S)
                local_sig="SIG$local_rc"
                case $local_rc in
                    134) local_sig="SIGABRT" ;;
                    136) local_sig="SIGFPE" ;;
                    138) local_sig="SIGBUS" ;;
                    139) local_sig="SIGSEGV" ;;
                esac
                mv "$tf" "$CRASH_DIR/crash_${local_sig}_${local_ts}_retry.html"
                echo -e "${RED}CRASH ($local_sig)${NC} in ${local_elapsed}s → reclassified as CRASH"
                ;;
            *)
                # Other error exit — treat as slow (handled error)
                TESTS_TIMEOUT=$((TESTS_TIMEOUT - 1))
                TESTS_SLOW=$((TESTS_SLOW + 1))
                local_ts=$(date +%Y%m%d_%H%M%S)
                mv "$tf" "$SLOW_DIR/slow_${local_ts}_${TESTS_SLOW}.html"
                echo -e "${GREEN}OK (error $local_rc)${NC} in ${local_elapsed}s → reclassified as ${YELLOW}SLOW${NC}"
                ;;
        esac
    done
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
END_TIME=$(date +%s)
TOTAL_ELAPSED=$((END_TIME - START_TIME))

echo ""
echo -e "${BOLD}════════════════════════════════════════════════${NC}"
echo -e "${BOLD}  Fuzzy Test Summary${NC}"
echo -e "${BOLD}════════════════════════════════════════════════${NC}"
echo "  Duration:       ${TOTAL_ELAPSED}s (${ROUND} rounds)"
echo "  Total tests:    $TESTS_RUN"
echo -e "  Passed:         ${GREEN}$TESTS_PASSED${NC}"
echo -e "  Crashes:        ${RED}$TESTS_CRASHED${NC}"
echo -e "  Slow:           ${YELLOW}$TESTS_SLOW${NC}"
echo -e "  Timeouts:       ${YELLOW}$TESTS_TIMEOUT${NC}"
echo -e "  Bad JSON:       ${YELLOW}$TESTS_BADJSON${NC}"
echo "  Handled errors: $TESTS_ERROR"
echo ""

if [[ $TESTS_CRASHED -gt 0 ]]; then
    echo -e "${RED}Crash-reproducing files saved to:${NC}"
    echo "  $CRASH_DIR/"
    ls -1 "$CRASH_DIR/" 2>/dev/null | tail -10
    echo ""
fi

if [[ $TESTS_SLOW -gt 0 ]]; then
    echo -e "${YELLOW}Slow test files saved to:${NC}"
    echo "  $SLOW_DIR/"
    ls -1 "$SLOW_DIR/" 2>/dev/null | tail -10
    echo ""
fi

if [[ $TESTS_TIMEOUT -gt 0 ]]; then
    echo -e "${YELLOW}Timeout-causing files (still hanging after ${RETRY_TIMEOUT}s retry):${NC}"
    echo "  $TIMEOUT_DIR/"
    ls -1 "$TIMEOUT_DIR/" 2>/dev/null | tail -10
    echo ""
fi

if [[ $TESTS_BADJSON -gt 0 ]]; then
    echo -e "${YELLOW}Bad JSON output files saved to:${NC}"
    echo "  $BADJSON_DIR/"
    echo ""
fi

# Throughput
if [[ $TOTAL_ELAPSED -gt 0 ]]; then
    THROUGHPUT=$((TESTS_RUN / TOTAL_ELAPSED))
    echo "  Throughput:     ~${THROUGHPUT} tests/sec"
fi

# Exit with failure if any crashes found
if [[ $TESTS_CRASHED -gt 0 ]]; then
    echo -e "${RED}FAILED: $TESTS_CRASHED crash(es) found${NC}"
    exit 1
fi

echo -e "${GREEN}PASSED: No crashes detected${NC}"
exit 0
