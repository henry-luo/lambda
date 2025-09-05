#!/bin/bash
set -e

echo "[+] Building fuzzer with libFuzzer..."

# Get absolute path to project root
PROJECT_ROOT="$(cd ../.. && pwd)"

# Create build directory
BUILD_DIR="${PROJECT_ROOT}/build_fuzz"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Configure with libFuzzer
cmake ${PROJECT_ROOT} \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -I${PROJECT_ROOT}" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=fuzzer,address,undefined" \
    -DCMAKE_BUILD_TYPE=Debug

# Build the fuzzer
make -j$(nproc)

# Build the fuzz target
clang++ \
    -fsanitize=fuzzer,address,undefined \
    -I../.. \
    -o lambda_fuzz \
    ../fuzz_target.cpp \
    -L. -llambda \
    -lstdc++

echo -e "\n[+] Fuzzer built successfully!"
echo "[+] Run with: ./lambda_fuzz -max_len=4096 -rss_limit_mb=2048 corpus/"

# Generate initial corpus if it doesn't exist
if [ ! -d "../test/fuzzy/corpus" ] || [ -z "$(ls -A ../test/fuzzy/corpus/)" ]; then
    echo -e "\n[+] Generating initial corpus..."
    mkdir -p ../test/fuzzy/corpus
    python3 ../test/fuzzy/scripts/generate_corpus.py
    
    echo -e "\n[+] Minimizing corpus..."
    mkdir -p corpus
    find ../test/fuzzy/corpus -type f -name "*.ls" | head -n 100 | xargs -I{} cp {} corpus/
    ./lambda_fuzz -merge=1 corpus/ corpus/ >/dev/null 2>&1 || true
    
    echo -e "\n[+] Corpus prepared. Found $(ls -1 corpus/ | wc -l) interesting inputs."
fi
