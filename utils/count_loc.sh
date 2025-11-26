#!/bin/bash

# Script to count files and lines of code in Lambda Script repository
# Usage: ./utils/count_loc.sh

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to count lines in a directory with optional exclusions
count_loc() {
    local dir="$1"
    local pattern="$2"
    local exclude_pattern="$3"

    if [ ! -d "$dir" ]; then
        echo "0 0"
        return
    fi

    local find_cmd="find \"$dir\" -type f"

    # Add file pattern if specified
    if [ -n "$pattern" ]; then
        find_cmd="$find_cmd -name \"$pattern\""
    fi

    # Add exclusions if specified
    if [ -n "$exclude_pattern" ]; then
        find_cmd="$find_cmd $exclude_pattern"
    fi

    local files=$(eval "$find_cmd" 2>/dev/null | wc -l | tr -d ' ')
    local lines=0

    if [ "$files" -gt 0 ]; then
        lines=$(eval "$find_cmd" 2>/dev/null | xargs wc -l 2>/dev/null | tail -1 | awk '{print $1}')
        if [ -z "$lines" ]; then
            lines=0
        fi
    fi

    echo "$files $lines"
}

# Function to format numbers with commas
format_number() {
    printf "%'d" "$1" 2>/dev/null || echo "$1"
}

echo -e "${BLUE}======================================${NC}"
echo -e "${BLUE}  Lambda Script - Lines of Code${NC}"
echo -e "${BLUE}======================================${NC}"
echo ""

# Initialize totals
total_files=0
total_lines=0

# Count ./lib
echo -e "${YELLOW}./lib${NC}"
result=$(count_loc "./lib" "*.c" "")
lib_files=$(echo "$result" | awk '{print $1}')
lib_lines=$(echo "$result" | awk '{print $2}')
result_h=$(count_loc "./lib" "*.h" "")
lib_files_h=$(echo "$result_h" | awk '{print $1}')
lib_lines_h=$(echo "$result_h" | awk '{print $2}')
lib_files=$((lib_files + lib_files_h))
lib_lines=$((lib_lines + lib_lines_h))
result_cpp=$(count_loc "./lib" "*.cpp" "")
lib_files_cpp=$(echo "$result_cpp" | awk '{print $1}')
lib_lines_cpp=$(echo "$result_cpp" | awk '{print $2}')
lib_files=$((lib_files + lib_files_cpp))
lib_lines=$((lib_lines + lib_lines_cpp))
result_hpp=$(count_loc "./lib" "*.hpp" "")
lib_files_hpp=$(echo "$result_hpp" | awk '{print $1}')
lib_lines_hpp=$(echo "$result_hpp" | awk '{print $2}')
lib_files=$((lib_files + lib_files_hpp))
lib_lines=$((lib_lines + lib_lines_hpp))

echo "  Files: $(format_number $lib_files)"
echo "  Lines: $(format_number $lib_lines)"
echo ""
total_files=$((total_files + lib_files))
total_lines=$((total_lines + lib_lines))

# Count ./lambda (excluding tree-sitter directories)
echo -e "${YELLOW}./lambda${NC} (excluding tree-sitter dirs)"

# Build exclusion pattern for tree-sitter directories
exclude_ts="-not -path \"./lambda/tree-sitter/*\" -not -path \"./lambda/tree-sitter-lambda/*\" -not -path \"./lambda/tree-sitter-javascript/*\""

# Count C files
result=$(count_loc "./lambda" "*.c" "$exclude_ts")
lambda_files=$(echo "$result" | awk '{print $1}')
lambda_lines=$(echo "$result" | awk '{print $2}')

# Count header files
result_h=$(count_loc "./lambda" "*.h" "$exclude_ts")
lambda_files_h=$(echo "$result_h" | awk '{print $1}')
lambda_lines_h=$(echo "$result_h" | awk '{print $2}')
lambda_files=$((lambda_files + lambda_files_h))
lambda_lines=$((lambda_lines + lambda_lines_h))

# Count C++ files
result_cpp=$(count_loc "./lambda" "*.cpp" "$exclude_ts")
lambda_files_cpp=$(echo "$result_cpp" | awk '{print $1}')
lambda_lines_cpp=$(echo "$result_cpp" | awk '{print $2}')
lambda_files=$((lambda_files + lambda_files_cpp))
lambda_lines=$((lambda_lines + lambda_lines_cpp))

# Count C++ headers
result_hpp=$(count_loc "./lambda" "*.hpp" "$exclude_ts")
lambda_files_hpp=$(echo "$result_hpp" | awk '{print $1}')
lambda_lines_hpp=$(echo "$result_hpp" | awk '{print $2}')
lambda_files=$((lambda_files + lambda_files_hpp))
lambda_lines=$((lambda_lines + lambda_lines_hpp))

echo "  Files: $(format_number $lambda_files)"
echo "  Lines: $(format_number $lambda_lines)"
echo ""
total_files=$((total_files + lambda_files))
total_lines=$((total_lines + lambda_lines))

# Count subdirectories of lambda (excluding tree-sitter)
for subdir in format input validator; do
    if [ -d "./lambda/$subdir" ]; then
        echo -e "${YELLOW}  ./lambda/$subdir${NC}"

        # Count C files
        result=$(count_loc "./lambda/$subdir" "*.c" "")
        sub_files=$(echo "$result" | awk '{print $1}')
        sub_lines=$(echo "$result" | awk '{print $2}')

        # Count header files
        result_h=$(count_loc "./lambda/$subdir" "*.h" "")
        sub_files_h=$(echo "$result_h" | awk '{print $1}')
        sub_lines_h=$(echo "$result_h" | awk '{print $2}')
        sub_files=$((sub_files + sub_files_h))
        sub_lines=$((sub_lines + sub_lines_h))

        # Count C++ files
        result_cpp=$(count_loc "./lambda/$subdir" "*.cpp" "")
        sub_files_cpp=$(echo "$result_cpp" | awk '{print $1}')
        sub_lines_cpp=$(echo "$result_cpp" | awk '{print $2}')
        sub_files=$((sub_files + sub_files_cpp))
        sub_lines=$((sub_lines + sub_lines_cpp))

        # Count C++ headers
        result_hpp=$(count_loc "./lambda/$subdir" "*.hpp" "")
        sub_files_hpp=$(echo "$result_hpp" | awk '{print $1}')
        sub_lines_hpp=$(echo "$result_hpp" | awk '{print $2}')
        sub_files=$((sub_files + sub_files_hpp))
        sub_lines=$((sub_lines + sub_lines_hpp))

        echo "    Files: $(format_number $sub_files)"
        echo "    Lines: $(format_number $sub_lines)"
        echo ""
    fi
done

# Count ./radiant
echo -e "${YELLOW}./radiant${NC}"
result=$(count_loc "./radiant" "*.c" "")
radiant_files=$(echo "$result" | awk '{print $1}')
radiant_lines=$(echo "$result" | awk '{print $2}')
result_h=$(count_loc "./radiant" "*.h" "")
radiant_files_h=$(echo "$result_h" | awk '{print $1}')
radiant_lines_h=$(echo "$result_h" | awk '{print $2}')
radiant_files=$((radiant_files + radiant_files_h))
radiant_lines=$((radiant_lines + radiant_lines_h))
result_cpp=$(count_loc "./radiant" "*.cpp" "")
radiant_files_cpp=$(echo "$result_cpp" | awk '{print $1}')
radiant_lines_cpp=$(echo "$result_cpp" | awk '{print $2}')
radiant_files=$((radiant_files + radiant_files_cpp))
radiant_lines=$((radiant_lines + radiant_lines_cpp))
result_hpp=$(count_loc "./radiant" "*.hpp" "")
radiant_files_hpp=$(echo "$result_hpp" | awk '{print $1}')
radiant_lines_hpp=$(echo "$result_hpp" | awk '{print $2}')
radiant_files=$((radiant_files + radiant_files_hpp))
radiant_lines=$((radiant_lines + radiant_lines_hpp))

echo "  Files: $(format_number $radiant_files)"
echo "  Lines: $(format_number $radiant_lines)"
echo ""
total_files=$((total_files + radiant_files))
total_lines=$((total_lines + radiant_lines))

# Count ./test (only .cpp and .ls files)
echo -e "${YELLOW}./test${NC} (only .cpp and .ls files)"
result=$(count_loc "./test" "*.cpp" "")
test_files=$(echo "$result" | awk '{print $1}')
test_lines=$(echo "$result" | awk '{print $2}')
result_ls=$(count_loc "./test" "*.ls" "")
test_files_ls=$(echo "$result_ls" | awk '{print $1}')
test_lines_ls=$(echo "$result_ls" | awk '{print $2}')
test_files=$((test_files + test_files_ls))
test_lines=$((test_lines + test_lines_ls))

echo "  Files: $(format_number $test_files)"
echo "  Lines: $(format_number $test_lines)"
echo ""
total_files=$((total_files + test_files))
total_lines=$((total_lines + test_lines))

# Count subdirectories of test
for subdir in $(find ./test -maxdepth 1 -type d ! -path "./test" | sort | sed 's|./test/||'); do
    if [ -d "./test/$subdir" ]; then
        echo -e "${YELLOW}  ./test/$subdir${NC}"

        # Count .cpp files
        result=$(count_loc "./test/$subdir" "*.cpp" "")
        sub_files=$(echo "$result" | awk '{print $1}')
        sub_lines=$(echo "$result" | awk '{print $2}')

        # Count .ls files
        result_ls=$(count_loc "./test/$subdir" "*.ls" "")
        sub_files_ls=$(echo "$result_ls" | awk '{print $1}')
        sub_lines_ls=$(echo "$result_ls" | awk '{print $2}')
        sub_files=$((sub_files + sub_files_ls))
        sub_lines=$((sub_lines + sub_lines_ls))

        echo "    Files: $(format_number $sub_files)"
        echo "    Lines: $(format_number $sub_lines)"
        echo ""
    fi
done

# Print totals
echo -e "${BLUE}======================================${NC}"
echo -e "${GREEN}TOTAL${NC}"
echo -e "${GREEN}  Files: $(format_number $total_files)${NC}"
echo -e "${GREEN}  Lines: $(format_number $total_lines)${NC}"
echo -e "${BLUE}======================================${NC}"
