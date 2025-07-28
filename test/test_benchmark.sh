#!/bin/bash

# Performance Benchmark Test Suite
# Tracks performance regressions across different test scenarios

set -e

echo "================================================"
echo "     Lambda Performance Benchmark Suite        "
echo "================================================"

# Configuration
BENCHMARK_RESULTS="test/benchmark_results.txt"
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

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

print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

# Function to run timed benchmark
run_benchmark() {
    local test_name="$1"
    local command="$2"
    local iterations="${3:-5}"
    
    print_status "🔥 Benchmarking: $test_name"
    
    local total_time=0
    local min_time=999999
    local max_time=0
    
    for i in $(seq 1 $iterations); do
        local start_time=$(date +%s.%N)
        eval "$command" >/dev/null 2>&1
        local end_time=$(date +%s.%N)
        local elapsed=$(echo "$end_time - $start_time" | bc -l)
        
        total_time=$(echo "$total_time + $elapsed" | bc -l)
        
        if (( $(echo "$elapsed < $min_time" | bc -l) )); then
            min_time=$elapsed
        fi
        
        if (( $(echo "$elapsed > $max_time" | bc -l) )); then
            max_time=$elapsed
        fi
        
        printf "  Run %d: %.3fs\n" $i $elapsed
    done
    
    local avg_time=$(echo "scale=3; $total_time / $iterations" | bc -l)
    
    printf "  📊 Average: %.3fs (min: %.3fs, max: %.3fs)\n" $avg_time $min_time $max_time
    
    # Store results
    echo "$TIMESTAMP,$test_name,$avg_time,$min_time,$max_time" >> "$BENCHMARK_RESULTS"
    
    echo ""
}

# Create benchmark results file with header if it doesn't exist
if [ ! -f "$BENCHMARK_RESULTS" ]; then
    echo "timestamp,test_name,avg_time,min_time,max_time" > "$BENCHMARK_RESULTS"
fi

print_status "🚀 Starting performance benchmarks..."
echo ""

# Benchmark 1: Full test suite execution
run_benchmark "Full Test Suite" "./test/test_all.sh" 3

# Benchmark 2: Individual test suites
run_benchmark "Library Tests Only" "cd test && gcc -o test_benchmark_lib.exe test_strbuf.c ../lib/strbuf.c -lcriterion && ./test_benchmark_lib.exe --verbose && rm test_benchmark_lib.exe" 5

# Benchmark 3: MIR JIT compilation
run_benchmark "MIR JIT Tests" "cd test && gcc -o test_benchmark_mir.exe test_mir.c ../build/*.o -lcriterion -lmir -lc2mir && ./test_benchmark_mir.exe --verbose && rm test_benchmark_mir.exe" 3

# Benchmark 4: Validator tests
run_benchmark "Validator Tests" "cd test && gcc -o test_benchmark_validator.exe test_validator.c -lcriterion && ./test_benchmark_validator.exe --verbose && rm test_benchmark_validator.exe" 3

# Benchmark 5: Lambda executable build time
run_benchmark "Lambda Build" "make clean && make build" 3

print_success "🎉 Benchmark suite completed!"
echo ""
print_status "📈 Results saved to: $BENCHMARK_RESULTS"
print_status "💡 Use 'tail -10 $BENCHMARK_RESULTS' to see recent results"
