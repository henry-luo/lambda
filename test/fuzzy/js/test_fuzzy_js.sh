#!/usr/bin/env bash
#
# Structured Fuzzy Test Runner for Lambda JS Engine
#
# Phases:
#   1. Seed corpus — small hand-written edge-case JS files
#   2. Mutation — mutate existing test/js/*.js files
#   3. Generation — grammar-based random JS code generation
#
# Usage:
#   ./test_fuzzy_js.sh                       # Default 5 min run
#   ./test_fuzzy_js.sh --duration=60         # 1 min quick run
#   ./test_fuzzy_js.sh --duration=3600       # 1 hour extended
#   ./test_fuzzy_js.sh --verbose             # Show individual test results
#   ./test_fuzzy_js.sh --generate-only       # Only generative fuzzing
#   ./test_fuzzy_js.sh --mutate-only         # Only mutation fuzzing
#   ./test_fuzzy_js.sh --seed=42             # Reproducible run

set -euo pipefail

# ── Paths ────────────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
LAMBDA_EXE="$PROJECT_ROOT/lambda.exe"
JS_TEST_DIR="$PROJECT_ROOT/test/js"
CORPUS_DIR="$SCRIPT_DIR/corpus"

TEMP_DIR="$PROJECT_ROOT/temp/js_fuzz"
CRASH_DIR="$SCRIPT_DIR/results/crashes"
TIMEOUT_DIR="$SCRIPT_DIR/results/timeouts"
GENERATOR="$SCRIPT_DIR/generators/js_gen.py"
MUTATOR="$SCRIPT_DIR/generators/js_mutator.py"

# ── Colors ───────────────────────────────────────────────────────────────────

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# ── Defaults ─────────────────────────────────────────────────────────────────

DURATION_SEC=300
TIMEOUT_SEC=10
GEN_COUNT=50
MUT_COUNT=3
MUT_SEEDS=50
VERBOSE=0
MUTATE_ONLY=0
GENERATE_ONLY=0
RANDOM_SEED=""

# ── Parse args ───────────────────────────────────────────────────────────────

while [[ $# -gt 0 ]]; do
    case "$1" in
        --duration=*)     DURATION_SEC="${1#*=}"; shift ;;
        --timeout=*)      TIMEOUT_SEC="${1#*=}"; shift ;;
        --gen-count=*)    GEN_COUNT="${1#*=}"; shift ;;
        --mut-count=*)    MUT_COUNT="${1#*=}"; shift ;;
        --mut-seeds=*)    MUT_SEEDS="${1#*=}"; shift ;;
        --mutate-only)    MUTATE_ONLY=1; shift ;;
        --generate-only)  GENERATE_ONLY=1; shift ;;
        --verbose|-v)     VERBOSE=1; shift ;;
        --seed=*)         RANDOM_SEED="${1#*=}"; shift ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --duration=SECONDS     Total fuzzing duration (default: 300)"
            echo "  --timeout=SECONDS      Per-test timeout (default: 10)"
            echo "  --gen-count=N          Generated files per round (default: 50)"
            echo "  --mut-count=N          Mutations per seed file (default: 3)"
            echo "  --mut-seeds=N          Max seeds to mutate per round (default: 50)"
            echo "  --mutate-only          Only run mutation-based fuzzing"
            echo "  --generate-only        Only run generation-based fuzzing"
            echo "  --verbose|-v           Show individual test results"
            echo "  --seed=N               Random seed for reproducibility"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"; exit 1 ;;
    esac
done

# ── Prerequisites ────────────────────────────────────────────────────────────

if [[ ! -f "$LAMBDA_EXE" ]]; then
    echo -e "${RED}Error: lambda.exe not found at $LAMBDA_EXE${NC}"
    echo "Run 'make build' first"
    exit 1
fi

if ! command -v python3 &>/dev/null; then
    echo -e "${RED}Error: python3 not found${NC}"
    exit 1
fi

# ── Setup ────────────────────────────────────────────────────────────────────

mkdir -p "$TEMP_DIR/gen" "$TEMP_DIR/mut"
mkdir -p "$CRASH_DIR" "$TIMEOUT_DIR"

# Statistics
TESTS_RUN=0
TESTS_PASSED=0
TESTS_CRASHED=0
TESTS_TIMEOUT=0
TESTS_ERROR=0
ROUND=0
START_TIME=$(date +%s)

log_verbose() {
    if [[ "$VERBOSE" -eq 1 ]]; then
        echo -e "$@"
    fi
}

# ── Run one JS test ──────────────────────────────────────────────────────────

run_one() {
    local js_file="$1"
    local label="$2"

    TESTS_RUN=$((TESTS_RUN + 1))

    # Run with timeout and --no-log to suppress debug output
    local exit_code=0
    timeout "$TIMEOUT_SEC" "$LAMBDA_EXE" js "$js_file" --no-log \
        >/dev/null 2>"$TEMP_DIR/stderr_$$.txt" || exit_code=$?

    local basename
    basename=$(basename "$js_file")

    case $exit_code in
        0)
            TESTS_PASSED=$((TESTS_PASSED + 1))
            log_verbose "  ${GREEN}PASS${NC}: $basename"
            ;;
        124)
            # Timeout
            TESTS_TIMEOUT=$((TESTS_TIMEOUT + 1))
            local ts
            ts=$(date +%Y%m%d_%H%M%S)
            cp "$js_file" "$TIMEOUT_DIR/timeout_${ts}_${TESTS_TIMEOUT}.js"
            echo -e "  ${YELLOW}TIMEOUT${NC}: $basename ($label)"
            ;;
        134|136|137|138|139)
            # SIGABRT=134, SIGFPE=136, SIGKILL=137, SIGBUS=138, SIGSEGV=139
            TESTS_CRASHED=$((TESTS_CRASHED + 1))
            local ts sig_name
            ts=$(date +%Y%m%d_%H%M%S)
            case $exit_code in
                134) sig_name="SIGABRT" ;;
                136) sig_name="SIGFPE" ;;
                137) sig_name="SIGKILL" ;;
                138) sig_name="SIGBUS" ;;
                139) sig_name="SIGSEGV" ;;
                *) sig_name="SIG$exit_code" ;;
            esac
            cp "$js_file" "$CRASH_DIR/crash_${sig_name}_${ts}_${TESTS_CRASHED}.js"
            echo -e "  ${RED}CRASH ($sig_name)${NC}: $basename ($label)"
            # Save stderr for crash analysis
            if [[ -f "$TEMP_DIR/stderr_$$.txt" ]]; then
                cp "$TEMP_DIR/stderr_$$.txt" "$CRASH_DIR/crash_${sig_name}_${ts}_${TESTS_CRASHED}.stderr"
            fi
            ;;
        *)
            # Non-crash error (parse error, runtime error) — expected for fuzzy inputs
            TESTS_ERROR=$((TESTS_ERROR + 1))
            log_verbose "  ${GREEN}OK (error $exit_code)${NC}: $basename"
            ;;
    esac

    rm -f "$TEMP_DIR/stderr_$$.txt"
}

# Run a batch of JS files from a directory
run_batch() {
    local dir="$1"
    local label="$2"

    for js_file in "$dir"/*.js; do
        [[ -f "$js_file" ]] || continue

        # Check time limit
        local now elapsed
        now=$(date +%s)
        elapsed=$((now - START_TIME))
        if [[ $elapsed -ge $DURATION_SEC ]]; then
            return 1
        fi

        run_one "$js_file" "$label"
    done
    return 0
}

# Print progress
print_progress() {
    local now elapsed remaining
    now=$(date +%s)
    elapsed=$((now - START_TIME))
    remaining=$((DURATION_SEC - elapsed))
    if [[ $remaining -lt 0 ]]; then remaining=0; fi

    echo -e "${CYAN}[Round $ROUND | ${elapsed}s/${DURATION_SEC}s]${NC}" \
        "run=$TESTS_RUN pass=$TESTS_PASSED" \
        "${RED}crash=$TESTS_CRASHED${NC}" \
        "${YELLOW}timeout=$TESTS_TIMEOUT${NC}" \
        "error=$TESTS_ERROR"
}

# ── Banner ───────────────────────────────────────────────────────────────────

echo -e "${BOLD}╔══════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}║   Lambda JS Engine — Fuzzy Test Runner           ║${NC}"
echo -e "${BOLD}╚══════════════════════════════════════════════════╝${NC}"
echo ""
echo "  Duration:     ${DURATION_SEC}s"
echo "  Timeout:      ${TIMEOUT_SEC}s per test"
echo "  Gen/round:    $GEN_COUNT"
echo "  Mut/seed:     $MUT_COUNT"
echo "  Max seeds:    $MUT_SEEDS"
if [[ -n "$RANDOM_SEED" ]]; then
    echo "  Seed:         $RANDOM_SEED"
fi
echo ""

# ── Phase 1: Seed corpus ────────────────────────────────────────────────────

echo -e "${BOLD}Phase 1: Seed corpus tests...${NC}"
if [[ -d "$CORPUS_DIR" ]]; then
    for js_file in "$CORPUS_DIR"/*.js; do
        [[ -f "$js_file" ]] || continue
        run_one "$js_file" "corpus"
    done
    echo "  Tested $(ls "$CORPUS_DIR"/*.js 2>/dev/null | wc -l | tr -d ' ') corpus files"
else
    echo "  No corpus directory found, skipping"
fi
echo ""

# ── Main loop: generate & mutate in rounds ───────────────────────────────────

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
    ROUND_SEED_ARG=""
    if [[ -n "$RANDOM_SEED" ]]; then
        ROUND_SEED_ARG="--seed=$((RANDOM_SEED + ROUND))"
    fi

    # --- Tier 2: Generative fuzzing ---
    if [[ $MUTATE_ONLY -eq 0 ]]; then
        rm -f "$TEMP_DIR/gen/"*.js 2>/dev/null || true
        python3 "$GENERATOR" \
            --count="$GEN_COUNT" \
            --output-dir="$TEMP_DIR/gen" \
            $ROUND_SEED_ARG \
            >/dev/null 2>&1 || true

        run_batch "$TEMP_DIR/gen" "gen-round-$ROUND" || break
    fi

    # --- Tier 3: Mutation fuzzing ---
    if [[ $GENERATE_ONLY -eq 0 ]] && [[ -d "$JS_TEST_DIR" ]]; then
        rm -f "$TEMP_DIR/mut/"*.js 2>/dev/null || true
        python3 "$MUTATOR" \
            --seeds-dir="$JS_TEST_DIR" \
            --output-dir="$TEMP_DIR/mut" \
            --count="$MUT_COUNT" \
            --max-seeds="$MUT_SEEDS" \
            $ROUND_SEED_ARG \
            >/dev/null 2>&1 || true

        run_batch "$TEMP_DIR/mut" "mut-round-$ROUND" || break
    fi

    print_progress
done

# ── Summary ──────────────────────────────────────────────────────────────────

END_TIME=$(date +%s)
TOTAL_TIME=$((END_TIME - START_TIME))

echo ""
echo -e "${BOLD}╔══════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}║   JS Fuzzy Test Summary                          ║${NC}"
echo -e "${BOLD}╚══════════════════════════════════════════════════╝${NC}"
echo ""
echo "  Total tests:   $TESTS_RUN"
echo "  Passed:        $TESTS_PASSED"
echo "  Errors:        $TESTS_ERROR (handled — expected for fuzz)"
echo "  Crashed:       $TESTS_CRASHED"
echo "  Timeouts:      $TESTS_TIMEOUT"
echo "  Duration:      ${TOTAL_TIME}s"
echo "  Tests/sec:     $((TESTS_RUN / (TOTAL_TIME + 1)))"
echo ""

if [[ "$TESTS_TIMEOUT" -gt 0 ]]; then
    echo -e "${YELLOW}⏱  $TESTS_TIMEOUT timeout(s) detected${NC}"
    ls "$TIMEOUT_DIR"/*.js 2>/dev/null | head -20 || true
    echo ""
fi

if [[ "$TESTS_CRASHED" -gt 0 ]]; then
    echo -e "${RED}FAILED: $TESTS_CRASHED crash(es) found${NC}"
    echo "Crash files saved in: $CRASH_DIR"
    ls -la "$CRASH_DIR"/*.js 2>/dev/null | head -20 || true
    exit 1
else
    echo -e "${GREEN}PASSED: No crashes detected${NC}"
    exit 0
fi
