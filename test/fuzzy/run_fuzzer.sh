#!/bin/bash
set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${SCRIPT_DIR}/../.."
LAMBDA_BIN="${PROJECT_ROOT}/lambda.exe"
OUTPUT_DIR="${SCRIPT_DIR}/results"
LOG_DIR="${SCRIPT_DIR}/log"
MAX_TESTS=500
MAX_LENGTH=500
MIN_LENGTH=10
SEED=$(date +%s)
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# Set up directories
mkdir -p "${OUTPUT_DIR}/crashes" "${OUTPUT_DIR}/hangs" "${LOG_DIR}"

# Set up log files
LOG_FILE="${LOG_DIR}/fuzz_${TIMESTAMP}.log"
SUMMARY_FILE="${LOG_DIR}/summary_${TIMESTAMP}.txt"
STATS_FILE="${LOG_DIR}/stats_${TIMESTAMP}.csv"

# Set environment for better crash detection
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:abort_on_error=1
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1

# Initialize stats file
echo "test_num,status,expression_length,depth,expression_hash,execution_time" > "${STATS_FILE}"

# Log function
log() {
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[${timestamp}] $1" | tee -a "${LOG_FILE}"
}

# Verify Lambda binary exists
if [ ! -f "${LAMBDA_BIN}" ]; then
    log "Error: Lambda binary not found at ${LAMBDA_BIN}"
    log "Please build the project first by running 'make' in the project root"
    exit 1
fi

# Calculate hash of a string for tracking
string_hash() {
    echo -n "$1" | md5sum | cut -d' ' -f1
}

# Function to generate random string
random_string() {
    local length=${1:-16}
    LC_ALL=C tr -dc 'a-zA-Z0-9!@#$%^&*()_+-=[]{}|;:,.<>?~' </dev/urandom | head -c "${length}"
}

# Function to generate random Lambda expression
generate_expression() {
    local depth=$1
    
    # Base case: simple expressions
    if [ ${depth} -le 0 ] || [ $((RANDOM % 3)) -eq 0 ]; then
        case $((RANDOM % 12)) in
            0) echo -n "x" ;;
            1) echo -n $((RANDOM % 100)) ;;
            2) echo -n $((RANDOM % 100)).$((RANDOM % 100)) ;;
            3) echo -n "\"$(random_string $((1 + RANDOM % 20)))\"" ;;
            4) echo -n "true" ;;
            5) echo -n "false" ;;
            6) echo -n "null" ;;
            7) echo -n "undefined" ;;
            8) echo -n "Infinity" ;;
            9) echo -n "NaN" ;;
            10) echo -n "'$(printf \\x$(printf %02x $((32 + RANDOM % 95))))'" ;;
            *) echo -n "_" ;;
        esac
        return
    fi
    
    # More aggressive depth reduction for complex expressions
    local new_depth=$((depth - 1 - (RANDOM % 3)))
    
    # Recursive case: generate a compound expression with more edge cases
    case $((RANDOM % 16)) in
        0|1) # Function application with multiple args
            echo -n "("
            generate_expression ${new_depth}
            local args=$((1 + RANDOM % 3))
            for ((i=0; i<args; i++)); do
                echo -n " "
                generate_expression ${new_depth}
            done
            echo -n ")"
            ;;
            
        2) # Lambda abstraction
            echo -n "(\\"
            echo -n "$(random_string 1)"
            echo -n " -> "
            generate_expression ${new_depth}
            echo -n ")"
            ;;
            
        3) # Let expression
            echo -n "(let "
            echo -n "$(random_string 1)"
            echo -n " = "
            generate_expression $((depth-2))
            echo -n " in "
            generate_expression $((depth-2))
            echo -n ")"
            ;;
            
        4) # If-then-else
            echo -n "(if "
            generate_expression $((depth-2))
            echo -n " then "
            generate_expression $((depth-2))
            echo -n " else "
            generate_expression $((depth-2))
            echo -n ")"
            ;;
            
        5|6|7) # Binary operators
            echo -n "("
            generate_expression $((depth-1))
            case $((RANDOM % 9)) in
                0) echo -n " + " ;;
                1) echo -n " - " ;;
                2) echo -n " * " ;;
                3) echo -n " / " ;;
                4) echo -n " ^ " ;;
                5) echo -n " == " ;;
                6) echo -n " != " ;;
                7) echo -n " < " ;;
                8) echo -n " > " ;;
            esac
            generate_expression $((depth-1))
            echo -n ")"
            ;;
            
        8|9) # Lists and records
            if [ $((RANDOM % 2)) -eq 0 ]; then
                echo -n "["
                local len=$((1 + RANDOM % 3))
                for ((i=0; i<len; i++)); do
                    [ ${i} -gt 0 ] && echo -n ", "
                    generate_expression $((depth-1))
                done
                echo -n "]"
            else
                echo -n "{"
                local len=$((1 + RANDOM % 3))
                for ((i=0; i<len; i++)); do
                    [ ${i} -gt 0 ] && echo -n ", "
                    echo -n "\"$(random_string $((3 + RANDOM % 8)))\": "
                    generate_expression $((depth-1))
                done
                echo -n "}"
            fi
            ;;
            
        10|11) # Pattern matching
            echo -n "(case "
            generate_expression $((depth-2))
            echo -n " of "
            local len=$((1 + RANDOM % 3))
            for ((i=0; i<len; i++)); do
                [ ${i} -gt 0 ] && echo -n " | "
                echo -n "\"$(random_string 1)\" -> "
                generate_expression $((depth-2))
            done
            echo -n ")"
            ;;
            
        12|13) # Function definition
            echo -n "let "
            echo -n "$(random_string $((3 + RANDOM % 5)))"
            local args=$((1 + RANDOM % 3))
            for ((i=0; i<args; i++)); do
                echo -n " \"$(random_string 1)\""
            done
            echo -n " = "
            generate_expression $((depth-1))
            echo -n " in "
            generate_expression $((depth-1))
            ;;
            
        *) # Recursive call with reduced depth
            generate_expression ${new_depth}
            ;;
    esac
}

# Function to run a single test case
run_test() {
    local input="$1"
    local test_num=$2
    local depth=$3
    local temp_file
    temp_file=$(mktemp)
    
    # Write test case to temp file
    echo "${input}" > "${temp_file}"
    local expr_hash=$(string_hash "${input}")
    
    # Log test case
    log "Running test #${test_num} (Depth: ${depth}, Hash: ${expr_hash})"
    log "Expression: ${input}"
    
    # Run with timeout to catch hangs
    local start_time=$(date +%s.%N)
    timeout 5s "${LAMBDA_BIN}" "${temp_file}" >/dev/null 2>&1
    local status=$?
    local end_time=$(date +%s.%N)
    local duration=$(echo "${end_time} - ${start_time}" | bc)
    
    # Log test result
    local status_str="PASS"
    local log_msg="Test #${test_num} completed in ${duration}s"
    
    # Check for crashes
    if [ ${status} -eq 139 ] || [ ${status} -eq 134 ] || [ ${status} -eq 136 ]; then
        # Save crashing input
        local crash_id=$(date +%s)_${test_num}
        local crash_file="${OUTPUT_DIR}/crashes/crash_${crash_id}.ls"
        cp "${temp_file}" "${crash_file}"
        
        # Save debug info
        echo -e "=== Input ===\n${input}" > "${crash_file}.debug"
        echo -e "\n=== Backtrace ===" >> "${crash_file}.debug"
        (gdb -batch -ex "run" -ex "bt" --args "${LAMBDA_BIN}" "${temp_file}" 2>&1 || 
         echo "No debug info available") >> "${crash_file}.debug" 2>&1
        
        status_str="CRASH (${status})"
        log_msg="[!] CRASH detected (status ${status}) - saved to ${crash_file}"
        log "${log_msg}"
        echo "${test_num},${status_str},${#input},${depth},${expr_hash},${duration}" >> "${STATS_FILE}"
        rm -f "${temp_file}"
        return 1
    
    # Check for timeouts
    elif [ ${status} -eq 124 ]; then
        local hang_id=$(date +%s)_${test_num}
        local hang_file="${OUTPUT_DIR}/hangs/hang_${hang_id}.ls"
        cp "${temp_file}" "${hang_file}"
        
        # Save debug info
        echo -e "=== Input ===\n${input}" > "${hang_file}.debug"
        
        status_str="HANG"
        log_msg="[!] HANG detected - saved to ${hang_file}"
        log "${log_msg}"
        echo "${test_num},${status_str},${#input},${depth},${expr_hash},${duration}" >> "${STATS_FILE}"
        rm -f "${temp_file}"
        return 1
    fi
    
    # Log successful test
    log "${log_msg} - ${status_str}"
    echo "${test_num},${status_str},${#input},${depth},${expr_hash},${duration}" >> "${STATS_FILE}"
    
    # Clean up
    rm -f "${temp_file}"
    return 0
}

main() {
    # Log start of fuzzing session
    log "=== Starting Lambda Fuzzer ==="
    log "Lambda binary: $(ls -l \"${LAMBDA_BIN}\")"
    log "Output directory: $(readlink -f \"${OUTPUT_DIR}\")"
    log "Log directory: $(readlink -f \"${LOG_DIR}\")"
    log "Maximum tests: ${MAX_TESTS}"
    log "Timestamp: ${TIMESTAMP}"
    
    echo "=== Lambda Fuzzer ==="
    echo "Lambda binary: ${LAMBDA_BIN}"
    echo "Output directory: ${OUTPUT_DIR}"
    echo "Log directory: ${LOG_DIR}"
    echo "Log file: ${LOG_FILE}"
    echo "Starting fuzzing with max ${MAX_TESTS} tests..."
    
    local start_time=$(date +%s)
    local crash_count=0
    local hang_count=0
    
    for ((i=1; i<=${MAX_TESTS}; i++)); do
        # Show progress
        if [ $((i % 10)) -eq 0 ] || [ ${i} -eq 1 ] || [ ${i} -eq ${MAX_TESTS} ]; then
            local progress=$((i * 100 / MAX_TESTS))
            local elapsed=$(( $(date +%s) - start_time ))
            local remaining=$(( (elapsed * (MAX_TESTS - i)) / i ))
            printf "\r[%3d%%] Test %d/%d | Elapsed: %02d:%02d | Remaining: %02d:%02d | Crashes: %d | Hangs: %d" \
                   "${progress}" "${i}" "${MAX_TESTS}" \
                   $((elapsed/60)) $((elapsed%60)) \
                   $((remaining/60)) $((remaining%60)) \
                   "${crash_count}" "${hang_count}"
        fi
        
        # Generate random expression with depth 3-7
        local depth=$((3 + RANDOM % 5))
        local expr
        expr=$(generate_expression ${depth})
        
        # Run test
        if ! run_test "${expr}" ${i} ${depth}; then
            if [ $? -eq 1 ]; then
                ((crash_count++))
            else
                ((hang_count++))
            fi
        fi
    done
    
    local end_time=$(date +%s)
    local total_time=$((end_time - start_time))
    
    # Generate summary
    local summary="\n=== Fuzzing Summary ===\n"
    summary+="Start time:    $(date -r ${start_time} '+%Y-%m-%d %H:%M:%S')\n"
    summary+="End time:      $(date -r ${end_time} '+%Y-%m-%d %H:%M:%S')\n"
    summary+="Total time:    $((total_time / 60))m $((total_time % 60))s\n"
    summary+="Tests run:     ${MAX_TESTS}\n"
    summary+="Test rate:     $(printf "%.2f" $(echo "scale=2; ${MAX_TESTS} / ${total_time}" | bc)) tests/sec\n"
    summary+="Crashes:       ${crash_count}\n"
    summary+="Hangs:         ${hang_count}\n"
    summary+="Pass rate:     $(( (MAX_TESTS - crash_count - hang_count) * 100 / MAX_TESTS ))%\n"
    
    # Save summary to file and display
    echo -e "${summary}" > "${SUMMARY_FILE}"
    echo -e "${summary}" | tee -a "${LOG_FILE}"
    
    # Show crash/hang locations if any
    if [ ${crash_count} -gt 0 ]; then
        echo -e "\nCrash logs saved to:\n  $(ls -1 "${OUTPUT_DIR}/crashes/"*.ls 2>/dev/null | head -n 3)"
        [ $(ls -1 "${OUTPUT_DIR}/crashes/"*.ls 2>/dev/null | wc -l) -gt 3 ] && echo "  ... and more"
    fi
    
    if [ ${hang_count} -gt 0 ]; then
        echo -e "\nHang logs saved to:\n  $(ls -1 "${OUTPUT_DIR}/hangs/"*.ls 2>/dev/null | head -n 3)"
        [ $(ls -1 "${OUTPUT_DIR}/hangs/"*.ls 2>/dev/null | wc -l) -gt 3 ] && echo "  ... and more"
    fi
    
    log "Detailed logs available in: ${LOG_FILE}"
    log "Summary report: ${SUMMARY_FILE}"
    log "Test statistics: ${STATS_FILE}"
    
    # Return non-zero if any issues found
    [ $((crash_count + hang_count)) -gt 0 ] && return 1
    return 0
}

# Start fuzzing
main "$@"
