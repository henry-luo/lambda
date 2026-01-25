#!/bin/bash

# Lambda Script Test Runner
# Runs individual Lambda scripts and compares with expected output

# Ensure git diff doesn't use a pager for consistent output
export GIT_PAGER=cat

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Function to show help and list available tests
show_help() {
    echo "Lambda Script Test Runner"
    echo ""
    echo "Usage: ./test/run.sh [options] [test_name]"
    echo ""
    echo "NOTE: Run this script from the project root directory"
    echo ""
    echo "Options:"
    echo "  -h, --help            Show this help and list available tests"
    echo "  -v, --verbose         Show verbose output including commands"
    echo "  -o, --output-only     Only show the script output, no diff"
    echo "  -g, --gui-diff        Use opendiff (macOS) for graphical diff visualization"
    echo ""
    echo "Arguments:"
    echo "  test_name            Name of the test to run (without .ls extension)"
    echo ""
    echo "Examples:"
    echo "  ./test/run.sh value               # Run value.ls test and show diff"
    echo "  ./test/run.sh --verbose decimal   # Run decimal.ls with verbose output"
    echo "  ./test/run.sh --output-only expr  # Just run expr.ls and show output"
    echo ""
    echo -e "${BLUE}Available Lambda Script Tests:${NC}"
    echo "=================================="
    
    # List all .ls files in test/lambda directory
    if [[ -d "./test/lambda" ]]; then
        local count=0
        local with_expected=0
        
        for ls_file in ./test/lambda/*.ls; do
            [[ -f "$ls_file" ]] || continue
            
            local base_name=$(basename "$ls_file" .ls)
            local expected_file="./test/lambda/${base_name}.txt"
            
            ((count++))
            
            if [[ -f "$expected_file" ]]; then
                echo -e "  ${GREEN}$base_name${NC} (has expected output)"
                ((with_expected++))
            else
                echo -e "  ${YELLOW}$base_name${NC} (no expected output)"
            fi
        done
        
        echo ""
        echo "Total: $count tests ($with_expected with expected output files)"
    else
        echo -e "${RED}Error: ./test/lambda directory not found${NC}"
        exit 1
    fi
}

# Function to run a Lambda script and capture output
run_lambda_script() {
    local script_path="$1"
    local output_file="$2"
    local verbose="$3"
    
    if [[ "$verbose" == true ]]; then
        echo -e "${CYAN}Running: ./lambda.exe $script_path${NC}"
    fi
    
    # Run the Lambda script and capture stdout only (matching gtest behavior)
    # stderr (including [NOTE] logs) is not captured
    if ./lambda.exe "$script_path" > "$output_file"; then
        return 0
    else
        return 1
    fi
}

# Function to show visual diff like visual_diff.sh
show_visual_diff() {
    local expected_file="$1"
    local actual_file="$2"
    local test_name="$3"
    
    echo -e "${CYAN}Comparing results for: ${test_name}${NC}"
    echo "Expected: $expected_file"
    echo "Actual: $actual_file"
    echo ""
    
    # Check if files exist
    if [[ ! -f "$expected_file" ]]; then
        echo -e "${YELLOW}No expected output file found${NC}"
        echo -e "${BLUE}Actual output:${NC}"
        echo "----------------------------------------"
        # Show filtered output (stdout only, no [NOTE] lines since stderr is not captured)
        cat "$actual_file" | grep -v "^TRACE:" | grep -v "^DEBUG:" 2>/dev/null || echo "(no output file)"
        echo "----------------------------------------"
        return
    fi
    
    if [[ ! -f "$actual_file" ]]; then
        echo -e "${RED}No actual output file generated${NC}"
        return
    fi
    
    # Create temporary file with filtered output for comparison
    # Filter out TRACE: and DEBUG: lines (stdout only, no [NOTE] lines since stderr is not captured)
    local filtered_actual=$(mktemp)
    cat "$actual_file" | grep -v "^TRACE:" | grep -v "^DEBUG:" > "$filtered_actual"
    
    # Compare files
    if diff -q "$expected_file" "$filtered_actual" > /dev/null 2>&1; then
        echo -e "${GREEN}✓ PASS: Output matches expected result${NC}"
        rm "$filtered_actual"
        return 0
    else
        echo -e "${RED}✗ FAIL: Output differs from expected${NC}"
        echo ""
        
        # Check if GUI diff is requested and available
        if [[ "$GUI_DIFF" == true ]]; then
            local gui_tool_used=false
            
            # Try opendiff first (macOS Xcode)
            if command -v opendiff >/dev/null 2>&1; then
                echo -e "${CYAN}Opening graphical diff with opendiff...${NC}"
                if opendiff "$expected_file" "$filtered_actual" 2>/dev/null; then
                    gui_tool_used=true
                    echo -e "${YELLOW}Press Enter to continue...${NC}"
                    read -r
                fi
            fi
            
            # Try other GUI diff tools if opendiff failed
            if [[ "$gui_tool_used" == false ]]; then
                for tool in "code --diff" "diff-so-fancy" "delta"; do
                    tool_cmd=$(echo "$tool" | cut -d' ' -f1)
                    if command -v "$tool_cmd" >/dev/null 2>&1; then
                        echo -e "${CYAN}Opening graphical diff with $tool...${NC}"
                        if $tool "$expected_file" "$filtered_actual" 2>/dev/null; then
                            gui_tool_used=true
                            break
                        fi
                    fi
                done
            fi
            
            if [[ "$gui_tool_used" == true ]]; then
                rm "$filtered_actual"
                return 1
            else
                echo -e "${YELLOW}Warning: No suitable GUI diff tool found, falling back to terminal diff${NC}"
            fi
        fi
        
        echo -e "${YELLOW}Differences (unified diff):${NC}"
        echo "----------------------------------------"
        
        # Try git diff first for colored output, fallback to regular diff
        if command -v git >/dev/null 2>&1; then
            # Use git diff for colored output (ignore exit code since files differ)
            # --no-pager ensures output goes directly to terminal without interactive paging
            git --no-pager diff --no-index --color=always "$expected_file" "$filtered_actual" || true
        else
            # Fallback to regular diff
            diff -u "$expected_file" "$filtered_actual" || true
        fi
        
        echo "----------------------------------------"
        rm "$filtered_actual"
        return 1
    fi
}

# Parse command line arguments
VERBOSE=false
OUTPUT_ONLY=false
GUI_DIFF=false
TEST_NAME=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -o|--output-only)
            OUTPUT_ONLY=true
            shift
            ;;
        -g|--gui-diff)
            GUI_DIFF=true
            shift
            ;;
        -*)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use --help to see available options"
            exit 1
            ;;
        *)
            if [[ -z "$TEST_NAME" ]]; then
                TEST_NAME="$1"
            else
                echo -e "${RED}Error: Multiple test names provided${NC}"
                echo "Usage: ./test/run.sh [options] [test_name]"
                exit 1
            fi
            shift
            ;;
    esac
done

# If no test name provided, show help
if [[ -z "$TEST_NAME" ]]; then
    show_help
    exit 0
fi

# Validate that lambda.exe exists
if [[ ! -f "./lambda.exe" ]]; then
    echo -e "${RED}Error: ./lambda.exe not found${NC}"
    echo "Please build the Lambda interpreter first"
    exit 1
fi

# Construct file paths
SCRIPT_FILE="./test/lambda/${TEST_NAME}.ls"
EXPECTED_FILE="./test/lambda/${TEST_NAME}.txt"
OUTPUT_DIR="./test_output"
ACTUAL_FILE="$OUTPUT_DIR/${TEST_NAME}.txt"

# Validate that the test script exists
if [[ ! -f "$SCRIPT_FILE" ]]; then
    echo -e "${RED}Error: Test script $SCRIPT_FILE not found${NC}"
    echo ""
    echo "Available tests:"
    for ls_file in ./test/lambda/*.ls; do
        [[ -f "$ls_file" ]] || continue
        base_name=$(basename "$ls_file" .ls)
        echo "  $base_name"
    done
    exit 1
fi

# Ensure output directory exists
mkdir -p "$OUTPUT_DIR"

echo -e "${BLUE}Lambda Script Test Runner${NC}"
echo "=========================="
echo ""

# Run the Lambda script
echo -e "${CYAN}Running test: ${TEST_NAME}${NC}"

if run_lambda_script "$SCRIPT_FILE" "$ACTUAL_FILE" "$VERBOSE"; then
    echo -e "${GREEN}✓ Script executed successfully${NC}"
else
    echo -e "${RED}✗ Script execution failed${NC}"
    echo ""
    echo -e "${YELLOW}Output/Error:${NC}"
    echo "----------------------------------------"
    cat "$ACTUAL_FILE" 2>/dev/null || echo "(no output)"
    echo "----------------------------------------"
    exit 1
fi

# Show output or diff based on options
if [[ "$OUTPUT_ONLY" == true ]]; then
    echo ""
    echo -e "${BLUE}Script Output:${NC}"
    echo "----------------------------------------"
    # Filter out debug/trace lines (stdout only, no [NOTE] lines since stderr is not captured)
    cat "$ACTUAL_FILE" | grep -v "^TRACE:" | grep -v "^DEBUG:"
    echo "----------------------------------------"
else
    echo ""
    if show_visual_diff "$EXPECTED_FILE" "$ACTUAL_FILE" "$TEST_NAME"; then
        exit 0
    else
        exit 1
    fi
fi
