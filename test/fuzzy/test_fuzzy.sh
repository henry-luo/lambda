#!/bin/bash
# Lambda Fuzzy Test Runner
# Runs corpus-based and generated fuzzy tests against Lambda

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
LAMBDA_EXE="$PROJECT_ROOT/lambda.exe"
CORPUS_DIR="$SCRIPT_DIR/corpus"
LAMBDA_TEST_DIR="$PROJECT_ROOT/test/lambda"
CRASH_DIR="$SCRIPT_DIR/crashes"
TIMEOUT_SEC=5
DURATION_SEC=300
VERBOSE=0
USE_LAMBDA_TESTS=1

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --duration=*)
            DURATION_SEC="${1#*=}"
            shift
            ;;
        --timeout=*)
            TIMEOUT_SEC="${1#*=}"
            shift
            ;;
        --verbose|-v)
            VERBOSE=1
            shift
            ;;
        --no-lambda-tests)
            USE_LAMBDA_TESTS=0
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--duration=SECONDS] [--timeout=SECONDS] [--verbose] [--no-lambda-tests]"
            exit 1
            ;;
    esac
done

# Check lambda executable
if [ ! -f "$LAMBDA_EXE" ]; then
    echo -e "${RED}Error: lambda.exe not found at $LAMBDA_EXE${NC}"
    echo "Run 'make build' first"
    exit 1
fi

# Create crash directory
mkdir -p "$CRASH_DIR"

# Statistics
TESTS_RUN=0
TESTS_PASSED=0
TESTS_CRASHED=0
TESTS_TIMEOUT=0
START_TIME=$(date +%s)

log_verbose() {
    if [ "$VERBOSE" -eq 1 ]; then
        echo "$@"
    fi
}

# Run a single test case
run_test() {
    local test_file="$1"
    local test_name=$(basename "$test_file")
    
    TESTS_RUN=$((TESTS_RUN + 1))
    log_verbose "  Testing: $test_name"
    
    # Run with timeout
    local result
    local exit_code
    result=$(timeout "$TIMEOUT_SEC" "$LAMBDA_EXE" "$test_file" 2>&1) || exit_code=$?
    
    if [ -z "$exit_code" ]; then
        exit_code=0
    fi
    
    case $exit_code in
        0)
            TESTS_PASSED=$((TESTS_PASSED + 1))
            log_verbose "    ${GREEN}PASS${NC}"
            ;;
        124)
            TESTS_TIMEOUT=$((TESTS_TIMEOUT + 1))
            log_verbose "    ${YELLOW}TIMEOUT${NC}"
            ;;
        134|136|137|138|139|141)
            # SIGABRT=134, SIGFPE=136, SIGKILL=137, SIGBUS=138, SIGSEGV=139, SIGPIPE=141
            TESTS_CRASHED=$((TESTS_CRASHED + 1))
            echo -e "    ${RED}CRASH (signal $exit_code)${NC}: $test_name"
            # Save crash case
            local crash_file="$CRASH_DIR/crash_$(date +%Y%m%d_%H%M%S)_${test_name}"
            cp "$test_file" "$crash_file"
            echo "    Saved to: $crash_file"
            ;;
        *)
            # Normal error (parse error, runtime error) - this is expected for fuzzy tests
            TESTS_PASSED=$((TESTS_PASSED + 1))
            log_verbose "    ${GREEN}OK (error handled: exit $exit_code)${NC}"
            ;;
    esac
}

# Generate random Lambda code
generate_random_code() {
    local length=$1
    local tokens=("1" "2" "3" "42" "100" "0" "-1" 
                  "true" "false" "null"
                  '"hello"' '"world"' '""' 
                  "+" "-" "*" "/" "%" "^"
                  "==" "!=" "<" ">" "<=" ">="
                  "and" "or" "not"
                  "(" ")" "[" "]" "{" "}" "<" ">"
                  "," ";" ":"
                  "let" "fn" "pn" "if" "else" "for" "in" "to"
                  "while" "break" "continue" "return" "var"
                  "x" "y" "z" "a" "b" "c" "n" "i" "j"
                  "=>" "->" "="
                  "sum" "len" "map" "filter" "reduce")
    
    local code=""
    for ((i=0; i<length; i++)); do
        local idx=$((RANDOM % ${#tokens[@]}))
        code="$code ${tokens[$idx]}"
    done
    echo "$code"
}

# Mutate a program with various strategies
mutate_program() {
    local input="$1"
    local mutation=$((RANDOM % 20))
    
    case $mutation in
        0) # Delete random character
            local pos=$((RANDOM % ${#input}))
            echo "${input:0:pos}${input:pos+1}"
            ;;
        1) # Insert random character
            local pos=$((RANDOM % ${#input}))
            local chars="()[]{}+-*/%^<>=!&|;:,.\"\\'0123456789abcdefghijklmnopqrstuvwxyz "
            local char="${chars:$((RANDOM % ${#chars})):1}"
            echo "${input:0:pos}$char${input:pos}"
            ;;
        2) # Replace with random token
            echo "$(generate_random_code 5)"
            ;;
        3) # Add deep nesting
            echo "((((($input)))))"
            ;;
        4) # Add very long string
            local long_str=$(printf '%0.s.' {1..100})
            echo "\"$long_str\""
            ;;
        5) # Add boundary numbers
            echo "$((RANDOM % 2 == 0 ? 9223372036854775807 : -9223372036854775808))"
            ;;
        6) # Duplicate input
            echo "$input $input"
            ;;
        7) # Add special values
            echo "inf nan null"
            ;;
        8) # Empty constructs
            echo "[] {} () <>"
            ;;
        9) # Delete random line
            echo "$input" | sed "$((RANDOM % $(echo "$input" | wc -l) + 1))d"
            ;;
        10) # Swap two random lines
            local lines=$(echo "$input" | wc -l)
            if [ "$lines" -gt 1 ]; then
                local line1=$((RANDOM % lines + 1))
                local line2=$((RANDOM % lines + 1))
                echo "$input" | sed "${line1}h;${line1}d;${line2}G"
            else
                echo "$input"
            fi
            ;;
        11) # Delete random function definition
            echo "$input" | sed '/^fn /d'
            ;;
        12) # Delete random let statement
            echo "$input" | sed '/^let /d'
            ;;
        13) # Corrupt function call (remove closing paren)
            echo "$input" | sed 's/)$//'
            ;;
        14) # Duplicate random function definition
            local fn_line=$(echo "$input" | grep -n '^fn ' | head -1 | cut -d: -f1)
            if [ -n "$fn_line" ]; then
                echo "$input" | sed "${fn_line}p"
            else
                echo "$input"
            fi
            ;;
        15) # Comment out random line
            local lines=$(echo "$input" | wc -l)
            if [ "$lines" -gt 0 ]; then
                local line=$((RANDOM % lines + 1))
                echo "$input" | sed "${line}s/^/\/\/ /"
            else
                echo "$input"
            fi
            ;;
        16) # Replace operator
            local ops=('+' '-' '*' '/' '%' '^' '==' '!=' '<' '>' 'and' 'or')
            local old_op="${ops[$((RANDOM % ${#ops[@]}))]}"
            local new_op="${ops[$((RANDOM % ${#ops[@]}))]}"
            echo "$input" | sed "s/${old_op}/${new_op}/g" 2>/dev/null || echo "$input"
            ;;
        17) # Remove function body
            echo "$input" | sed 's/=> .*/=>/'
            ;;
        18) # Add type error
            echo "$input" | sed 's/\([0-9]\+\)/"\1"/g'
            ;;
        19) # Pass through (no mutation)
            echo "$input"
            ;;
    esac
}

echo "=========================================="
echo "Lambda Fuzzy Test Runner"
echo "=========================================="
echo "Duration: ${DURATION_SEC}s"
echo "Timeout per test: ${TIMEOUT_SEC}s"
echo "Corpus: $CORPUS_DIR"
if [ "$USE_LAMBDA_TESTS" -eq 1 ]; then
    echo "Lambda tests: $LAMBDA_TEST_DIR (enabled)"
else
    echo "Lambda tests: disabled"
fi
echo "Crashes saved to: $CRASH_DIR"
echo ""

# Phase 1: Run corpus tests
echo "Phase 1: Testing seed corpus..."
if [ -d "$CORPUS_DIR" ]; then
    for category in valid edge_cases; do
        if [ -d "$CORPUS_DIR/$category" ]; then
            echo "  Category: $category"
            for test_file in "$CORPUS_DIR/$category"/*.ls; do
                if [ -f "$test_file" ]; then
                    run_test "$test_file"
                fi
            done
        fi
    done
fi
echo ""

# Phase 2: Mutation testing (with real Lambda test scripts)
echo "Phase 2: Mutation testing..."
TEMP_FILE=$(mktemp /tmp/fuzzy_XXXXXX.ls)
trap "rm -f $TEMP_FILE" EXIT

# Get seed programs from corpus
SEED_PROGRAMS=()
SEED_FILES=()
if [ -d "$CORPUS_DIR/valid" ]; then
    for f in "$CORPUS_DIR/valid"/*.ls; do
        if [ -f "$f" ]; then
            SEED_PROGRAMS+=("$(cat "$f")")
            SEED_FILES+=("$f")
        fi
    done
fi

# Add Lambda test scripts as seeds
if [ "$USE_LAMBDA_TESTS" -eq 1 ] && [ -d "$LAMBDA_TEST_DIR" ]; then
    echo "  Loading Lambda test scripts as fuzzing seeds..."
    LAMBDA_SEEDS_COUNT=0
    for f in "$LAMBDA_TEST_DIR"/*.ls; do
        if [ -f "$f" ]; then
            # Skip very large test files (>50KB) to keep mutations fast
            size=$(stat -f%z "$f" 2>/dev/null || stat -c%s "$f" 2>/dev/null || echo 0)
            if [ "$size" -lt 51200 ]; then
                SEED_PROGRAMS+=("$(cat "$f")")
                SEED_FILES+=("$f")
                LAMBDA_SEEDS_COUNT=$((LAMBDA_SEEDS_COUNT + 1))
            fi
        fi
    done
    log_verbose "  Loaded $LAMBDA_SEEDS_COUNT Lambda test scripts"
fi

# Add some basic seeds if none found
if [ ${#SEED_PROGRAMS[@]} -eq 0 ]; then
    SEED_PROGRAMS+=("1 + 2")
    SEED_FILES+=("basic_seed")
    SEED_PROGRAMS+=("let x = 5; x * 2")
    SEED_FILES+=("basic_seed")
    SEED_PROGRAMS+=("fn f(x) => x + 1")
    SEED_FILES+=("basic_seed")
fi

echo "  Using ${#SEED_PROGRAMS[@]} seed programs for mutation"

# Allocate 70% of time to mutation, 30% to random generation
MUTATION_END_TIME=$((START_TIME + DURATION_SEC * 70 / 100))
RANDOM_END_TIME=$((START_TIME + DURATION_SEC))

MUTATION_TESTS=0
while [ $(($(date +%s))) -lt "$MUTATION_END_TIME" ]; do
    # Pick random seed
    seed_idx=$((RANDOM % ${#SEED_PROGRAMS[@]}))
    program="${SEED_PROGRAMS[$seed_idx]}"
    
    # Apply mutation
    mutated=$(mutate_program "$program")
    echo "$mutated" > "$TEMP_FILE"
    
    run_test "$TEMP_FILE"
    MUTATION_TESTS=$((MUTATION_TESTS + 1))
    
    # Progress every 100 tests
    if [ $((MUTATION_TESTS % 100)) -eq 0 ]; then
        elapsed=$(($(date +%s) - START_TIME))
        echo "  Progress: $MUTATION_TESTS mutations, ${elapsed}s elapsed"
    fi
done
echo "  Completed $MUTATION_TESTS mutation tests"
echo ""

# Phase 3: Random generation
echo "Phase 3: Random generation testing..."
RANDOM_TESTS=0
while [ $(($(date +%s))) -lt "$RANDOM_END_TIME" ]; do
    # Generate random code of varying length
    length=$((5 + RANDOM % 50))
    code=$(generate_random_code $length)
    echo "$code" > "$TEMP_FILE"
    
    run_test "$TEMP_FILE"
    RANDOM_TESTS=$((RANDOM_TESTS + 1))
    
    # Progress every 100 tests
    if [ $((RANDOM_TESTS % 100)) -eq 0 ]; then
        elapsed=$(($(date +%s) - START_TIME))
        echo "  Progress: $RANDOM_TESTS random tests, ${elapsed}s elapsed"
    fi
done
echo "  Completed $RANDOM_TESTS random tests"
echo ""

# Summary
END_TIME=$(date +%s)
TOTAL_TIME=$((END_TIME - START_TIME))

echo "=========================================="
echo "Fuzzy Test Summary"
echo "=========================================="
echo "Total tests:   $TESTS_RUN"
echo "Passed:        $TESTS_PASSED"
echo "Crashed:       $TESTS_CRASHED"
echo "Timeouts:      $TESTS_TIMEOUT"
echo "Duration:      ${TOTAL_TIME}s"
echo "Tests/sec:     $((TESTS_RUN / (TOTAL_TIME + 1)))"
echo ""

if [ "$TESTS_CRASHED" -gt 0 ]; then
    echo -e "${RED}⚠️  $TESTS_CRASHED crash(es) detected!${NC}"
    echo "Crash files saved in: $CRASH_DIR"
    ls -la "$CRASH_DIR"/*.ls 2>/dev/null || true
    exit 1
else
    echo -e "${GREEN}✅ No crashes detected${NC}"
    exit 0
fi
