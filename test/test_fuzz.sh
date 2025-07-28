#!/bin/bash

# Fuzzing Test Suite
# Generates random inputs to test edge cases and robustness

set -e

echo "================================================"
echo "        Lambda Fuzzing Test Suite              "
echo "================================================"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_status() {
    echo -e "${BLUE}$1${NC}"
}

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

# Generate random test inputs
generate_random_inputs() {
    local output_dir="$1"
    local count="$2"
    
    mkdir -p "$output_dir"
    
    print_status "🎲 Generating $count random test inputs..."
    
    for i in $(seq 1 $count); do
        local file="$output_dir/fuzz_input_$i.ls"
        
        # Generate random Lambda script content
        cat > "$file" <<EOF
// Auto-generated fuzz test $i
$(generate_random_lambda_content)
EOF
    done
    
    print_success "Generated $count fuzz test files in $output_dir"
}

# Generate random Lambda script content
generate_random_lambda_content() {
    local content_types=("string" "number" "array" "object" "function")
    local selected_type=${content_types[$((RANDOM % ${#content_types[@]}))]}
    
    case "$selected_type" in
        "string")
            echo "\"$(generate_random_string)\""
            ;;
        "number")
            echo "$((RANDOM % 10000))"
            ;;
        "array")
            echo "[$(generate_random_string), $((RANDOM % 100)), null]"
            ;;
        "object")
            echo "{ name: \"$(generate_random_string)\", value: $((RANDOM % 100)) }"
            ;;
        "function")
            echo "fn(x) { x + $((RANDOM % 10)) }"
            ;;
    esac
}

# Generate random string with special characters
generate_random_string() {
    local chars="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()_+-=[]{}|;:,.<>?"
    local length=$((RANDOM % 20 + 5))
    local result=""
    
    for i in $(seq 1 $length); do
        result+="${chars:$((RANDOM % ${#chars})):1}"
    done
    
    echo "$result"
}

# Run fuzzing tests on different components
run_fuzz_tests() {
    local fuzz_dir="$1"
    local component="$2"
    
    print_status "🔥 Fuzzing $component with random inputs..."
    
    local total_tests=0
    local crashed_tests=0
    local passed_tests=0
    
    for input_file in "$fuzz_dir"/*.ls; do
        if [ -f "$input_file" ]; then
            total_tests=$((total_tests + 1))
            
            case "$component" in
                "parser")
                    # Test lambda parsing
                    if timeout 5s ./lambda.exe "$input_file" >/dev/null 2>&1; then
                        passed_tests=$((passed_tests + 1))
                    else
                        local exit_code=$?
                        if [ $exit_code -eq 124 ]; then  # timeout
                            print_warning "Timeout on $input_file"
                        elif [ $exit_code -gt 128 ]; then  # crash
                            crashed_tests=$((crashed_tests + 1))
                            print_error "Crash on $input_file (exit code: $exit_code)"
                        fi
                    fi
                    ;;
                "validator")
                    # Test validator with random schemas
                    if timeout 5s ./lambda.exe validate "$input_file" >/dev/null 2>&1; then
                        passed_tests=$((passed_tests + 1))
                    else
                        local exit_code=$?
                        if [ $exit_code -eq 124 ]; then
                            print_warning "Timeout on validator test $input_file"
                        elif [ $exit_code -gt 128 ]; then
                            crashed_tests=$((crashed_tests + 1))
                            print_error "Validator crash on $input_file"
                        fi
                    fi
                    ;;
            esac
        fi
    done
    
    echo ""
    print_status "📊 Fuzz Test Results for $component:"
    echo "   Total Tests: $total_tests"
    echo "   Passed: $passed_tests"
    echo "   Crashed: $crashed_tests"
    echo "   Timeouts: $((total_tests - passed_tests - crashed_tests))"
    
    if [ $crashed_tests -gt 0 ]; then
        print_error "⚠️  Found $crashed_tests crashes - investigate inputs that caused crashes"
        return 1
    else
        print_success "✅ No crashes found in $component fuzzing"
        return 0
    fi
}

# Generate edge case inputs
generate_edge_cases() {
    local output_dir="$1"
    
    mkdir -p "$output_dir"
    
    print_status "⚡ Generating edge case test inputs..."
    
    # Empty file
    touch "$output_dir/empty.ls"
    
    # Very large file
    for i in $(seq 1 1000); do
        echo "data_$i = \"$(generate_random_string)\"" >> "$output_dir/large.ls"
    done
    
    # Special characters
    cat > "$output_dir/special_chars.ls" <<'EOF'
"Unicode: 你好世界 🌍 ñoño"
"Escape sequences: \n\t\r\\\""
"Control chars: $(printf '\x00\x01\x02\x03')"
EOF
    
    # Deeply nested structures
    local nested_content="{"
    for i in $(seq 1 100); do
        nested_content+=" level_$i: {"
    done
    for i in $(seq 1 100); do
        nested_content+=" }"
    done
    nested_content+=" }"
    echo "$nested_content" > "$output_dir/deeply_nested.ls"
    
    # Binary data (might cause issues)
    dd if=/dev/urandom of="$output_dir/binary_data.ls" bs=1024 count=1 2>/dev/null
    
    print_success "Generated edge case test files"
}

# Main execution
print_status "🚀 Starting fuzzing test suite..."

# Check if lambda executable exists
if [ ! -f "../lambda.exe" ]; then
    print_error "Lambda executable not found. Run 'make build' first."
    exit 1
fi

cd test

# Create fuzzing directories
FUZZ_DIR="fuzz_inputs"
EDGE_DIR="edge_cases"

rm -rf "$FUZZ_DIR" "$EDGE_DIR"

# Generate test inputs
generate_random_inputs "$FUZZ_DIR" 50
generate_edge_cases "$EDGE_DIR"

# Copy lambda executable to test directory for easier access
cp ../lambda.exe .

total_failures=0

# Run fuzzing tests
echo ""
if ! run_fuzz_tests "$FUZZ_DIR" "parser"; then
    total_failures=$((total_failures + 1))
fi

echo ""
if ! run_fuzz_tests "$FUZZ_DIR" "validator"; then
    total_failures=$((total_failures + 1))
fi

echo ""
if ! run_fuzz_tests "$EDGE_DIR" "parser"; then
    total_failures=$((total_failures + 1))
fi

# Cleanup
rm -f lambda.exe
# rm -rf "$FUZZ_DIR" "$EDGE_DIR"  # Keep for debugging

echo ""
if [ $total_failures -eq 0 ]; then
    print_success "🎉 Fuzzing test suite completed successfully!"
    print_status "💡 No crashes or critical issues found"
else
    print_error "⚠️  Fuzzing found $total_failures issues"
    print_status "💡 Check generated inputs in $FUZZ_DIR and $EDGE_DIR for debugging"
    exit 1
fi
