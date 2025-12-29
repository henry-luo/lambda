#!/bin/bash
# C/C++ Compilation Performance Benchmark for Lambda
# Compares build performance on different hardware

set -e

echo "=========================================="
echo "  Lambda C/C++ Compilation Benchmark"
echo "=========================================="
echo ""

# Get system info
echo "=== System Information ==="
echo "CPU: $(cat /proc/cpuinfo | grep 'model name' | head -1 | cut -d: -f2 | xargs)"
echo "Cores: $(nproc)"
echo "RAM: $(cat /proc/meminfo | grep MemTotal | awk '{printf "%.1f GB", $2/1024/1024}')"
echo "Compiler: $(gcc --version | head -1)"
echo "ccache: $(ccache --version 2>/dev/null | head -1 || echo 'not installed')"
echo ""

# Test 1: Single file compilation speed
echo "=== Test 1: Single File Compilation Speed ==="
echo "Compiling a simple C++ file (10 iterations)..."

cat > /tmp/bench_simple.cpp << 'EOF'
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>

int main() {
    std::vector<std::string> items = {"apple", "banana", "cherry"};
    std::sort(items.begin(), items.end());
    for (const auto& item : items) {
        std::cout << item << std::endl;
    }
    return 0;
}
EOF

SIMPLE_TOTAL=0
for i in {1..10}; do
    START=$(date +%s%N)
    g++ -O2 -std=c++17 /tmp/bench_simple.cpp -o /tmp/bench_simple 2>/dev/null
    END=$(date +%s%N)
    TIME=$(( (END - START) / 1000000 )) # milliseconds
    SIMPLE_TOTAL=$((SIMPLE_TOTAL + TIME))
done
SIMPLE_AVG=$((SIMPLE_TOTAL / 10))
echo "  Average: ${SIMPLE_AVG}ms per compile"
echo ""

# Test 2: Template-heavy C++ compilation
echo "=== Test 2: Template-Heavy C++ (5 iterations) ==="
echo "Compiling complex template code..."

cat > /tmp/bench_template.cpp << 'EOF'
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <memory>
#include <functional>

template<typename T>
class Container {
    std::vector<T> data;
public:
    void add(T item) { data.push_back(item); }
    template<typename Func>
    void forEach(Func f) {
        std::for_each(data.begin(), data.end(), f);
    }
};

template<typename K, typename V>
class Cache {
    std::map<K, V> storage;
public:
    void put(K key, V value) { storage[key] = value; }
    V get(K key) { return storage[key]; }
};

int main() {
    Container<int> nums;
    for (int i = 0; i < 100; i++) nums.add(i);

    Cache<std::string, int> cache;
    cache.put("test", 42);

    nums.forEach([](int x) { std::cout << x << " "; });
    return 0;
}
EOF

TEMPLATE_TOTAL=0
for i in {1..5}; do
    START=$(date +%s%N)
    g++ -O2 -std=c++17 /tmp/bench_template.cpp -o /tmp/bench_template 2>/dev/null
    END=$(date +%s%N)
    TIME=$(( (END - START) / 1000000 ))
    TEMPLATE_TOTAL=$((TEMPLATE_TOTAL + TIME))
done
TEMPLATE_AVG=$((TEMPLATE_TOTAL / 5))
echo "  Average: ${TEMPLATE_AVG}ms per compile"
echo ""

# Test 3: Multi-file project compilation
echo "=== Test 3: Multi-File Project (3 iterations) ==="
echo "Compiling 10-file project..."

mkdir -p /tmp/bench_project
for i in {1..10}; do
    cat > /tmp/bench_project/file$i.cpp << EOF
#include <iostream>
#include <vector>
#include <string>

void function$i() {
    std::vector<int> data;
    for (int j = 0; j < 1000; j++) {
        data.push_back(j * $i);
    }
    std::cout << "Function $i: " << data.size() << std::endl;
}
EOF
done

cat > /tmp/bench_project/main.cpp << 'EOF'
#include <iostream>
extern void function1(); extern void function2();
extern void function3(); extern void function4();
extern void function5(); extern void function6();
extern void function7(); extern void function8();
extern void function9(); extern void function10();

int main() {
    function1(); function2(); function3(); function4(); function5();
    function6(); function7(); function8(); function9(); function10();
    return 0;
}
EOF

MULTI_TOTAL=0
for i in {1..3}; do
    START=$(date +%s%N)
    g++ -O2 -std=c++17 /tmp/bench_project/*.cpp -o /tmp/bench_project/program 2>/dev/null
    END=$(date +%s%N)
    TIME=$(( (END - START) / 1000000 ))
    MULTI_TOTAL=$((MULTI_TOTAL + TIME))
done
MULTI_AVG=$((MULTI_TOTAL / 3))
echo "  Average: ${MULTI_AVG}ms per compile"
echo ""

# Test 4: Parallel compilation test
echo "=== Test 4: Parallel Compilation (make -j) ==="
echo "Building Lambda project (1 clean build)..."

# Find Lambda project root (script is in utils/ directory)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"
if [ -f "Makefile" ]; then
    make clean-all > /dev/null 2>&1

    START=$(date +%s%N)
    time make build -j$(nproc) 2>&1 | tail -3
    END=$(date +%s%N)

    LAMBDA_TIME=$(( (END - START) / 1000000000 )) # seconds
    echo "  Lambda full build: ${LAMBDA_TIME}s"
else
    echo "  Skipped (not in Lambda directory)"
fi
echo ""

# Test 5: ccache effectiveness
if command -v ccache >/dev/null 2>&1; then
    echo "=== Test 5: ccache Performance ==="
    ccache -z > /dev/null 2>&1

    echo "First compile (cold cache):"
    START=$(date +%s%N)
    g++ -O2 -std=c++17 /tmp/bench_template.cpp -o /tmp/bench_ccache1 2>/dev/null
    END=$(date +%s%N)
    COLD=$((( END - START) / 1000000 ))
    echo "  Time: ${COLD}ms"

    echo "Second compile (warm cache):"
    START=$(date +%s%N)
    g++ -O2 -std=c++17 /tmp/bench_template.cpp -o /tmp/bench_ccache2 2>/dev/null
    END=$(date +%s%N)
    WARM=$(( (END - START) / 1000000 ))
    echo "  Time: ${WARM}ms"

    if command -v bc >/dev/null 2>&1; then
        SPEEDUP=$(echo "scale=1; $COLD / $WARM" | bc)
        echo "  Speedup: ${SPEEDUP}x"
    else
        # Fallback if bc is not available
        if [ $WARM -gt 0 ]; then
            SPEEDUP=$((COLD / WARM))
            echo "  Speedup: ~${SPEEDUP}x"
        fi
    fi

    ccache -s | grep -E "cache hit|cache miss|Hits"
fi
echo ""

# Summary
echo "=========================================="
echo "  Benchmark Summary"
echo "=========================================="
echo "Simple C++ file:      ${SIMPLE_AVG}ms"
echo "Template-heavy C++:   ${TEMPLATE_AVG}ms"
echo "Multi-file project:   ${MULTI_AVG}ms"
if [ -n "$LAMBDA_TIME" ]; then
    echo "Lambda full build:    ${LAMBDA_TIME}s"
fi
echo ""
echo "=== Benchmark Score (lower is better) ==="
SCORE=$((SIMPLE_AVG + TEMPLATE_AVG + MULTI_AVG))
echo "Total: ${SCORE}ms (combined simple+template+multi)"
echo ""
echo "Typical scores for reference:"
echo "  - Mac Mini M2:      ~200-300ms"
echo "  - Mac Mini M1:      ~300-400ms"
echo "  - Desktop i7:       ~400-600ms"
echo "  - Laptop i5:        ~600-900ms"
echo "  - Budget Celeron:   ~900-1500ms"
echo ""

# Cleanup
rm -f /tmp/bench_simple* /tmp/bench_template* /tmp/bench_ccache*
rm -rf /tmp/bench_project

echo "Benchmark complete!"
echo ""
echo "To compare with another machine:"
echo "  1. Copy this script to the other machine"
echo "  2. Run: bash benchmark_compile.sh"
echo "  3. Compare the 'Total' scores"
