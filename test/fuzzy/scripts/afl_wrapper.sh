#!/bin/bash
set -e

# Configuration
BUILD_DIR="../../build_afl"
FUZZ_TARGET="lambda_fuzz"
CORPUS_DIR="../corpus"
OUTPUT_DIR="../afl_output"
TIMEOUT="30m"  # 30 minutes per fuzzer instance
MEMORY_LIMIT="500MB"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if AFL++ is installed
if ! command -v afl-fuzz &> /dev/null; then
    echo -e "${RED}Error: AFL++ not found. Please install it first.${NC}"
    exit 1
fi

# Create build directory
echo -e "${YELLOW}[+] Creating build directory: ${BUILD_DIR}${NC}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Configure with AFL instrumentation
echo -e "${YELLOW}[+] Configuring build with AFL instrumentation...${NC}"
CC=afl-gcc CXX=afl-g++ cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build the fuzz target
echo -e "${YELLOW}[+] Building fuzz target...${NC}"
make -j$(nproc)

# Check if the fuzz target was built
if [ ! -f "${FUZZ_TARGET}" ]; then
    echo -e "${RED}Error: Fuzz target '${FUZZ_TARGET}' not found in build directory.${NC}"
    exit 1
fi

# Prepare corpus
echo -e "${YELLOW}[+] Preparing corpus...${NC}"
mkdir -p "${CORPUS_DIR}"

# Copy existing test files if they exist
if [ -d "../../test/lambda" ]; then
    echo -e "${YELLOW}[+] Copying test files to corpus...${NC}"
    find "../../test/lambda" -name "*.ls" -exec cp {} "${CORPUS_DIR}/" \;
fi

# If no test files were found, generate some
if [ -z "$(ls -A ${CORPUS_DIR})" ]; then
    echo -e "${YELLOW}[!] No test files found, generating basic corpus...${NC}"
    python3 ../scripts/generate_corpus.py
fi

# Create output directory
mkdir -p "${OUTPUT_DIR}"

# Run AFL++
echo -e "${YELLOW}[+] Starting AFL++ fuzzer...${NC}"
echo -e "${YELLOW}    - Input corpus: ${CORPUS_DIR}${NC}"
echo -e "${YELLOW}    - Output directory: ${OUTPUT_DIR}${NC}"
echo -e "${YELLOW}    - Timeout: ${TIMEOUT} per instance${NC}"

echo -e "\n${GREEN}[*] Starting main fuzzer instance...${NC}"
timeout "${TIMEOUT}" \
    afl-fuzz \
    -i "${CORPUS_DIR}" \
    -o "${OUTPUT_DIR}" \
    -m "${MEMORY_LIMIT}" \
    -M "fuzzer01" \
    -- "./${FUZZ_TARGET}" @@

# Check if we should start secondary instances
if [ -x "$(command -v afl-gcc)" ] && [ -x "$(command -v afl-g++)" ]; then
    echo -e "\n${GREEN}[*] Starting secondary fuzzer instances...${NC}"
    
    # Start 3 more instances in background
    for i in {2..4}; do
        echo -e "${YELLOW}    - Starting fuzzer instance ${i}...${NC}"
        timeout "${TIMEOUT}" \
            afl-fuzz \
            -i "${CORPUS_DIR}" \
            -o "${OUTPUT_DIR}" \
            -m "${MEMORY_LIMIT}" \
            -S "fuzzer0${i}" \
            -- "./${FUZZ_TARGET}" @@ &
    done
    
    # Wait for all background jobs to complete
    wait
fi

echo -e "\n${GREEN}[+] Fuzzing completed!${NC}"
echo -e "${YELLOW}[*] Results saved to: ${OUTPUT_DIR}${NC}"

# Show crash summary
if [ -d "${OUTPUT_DIR}/fuzzer01/crashes" ]; then
    CRASH_COUNT=$(find "${OUTPUT_DIR}/fuzzer01/crashes" -type f ! -name "README.txt" | wc -l)
    echo -e "${YELLOW}[*] Found ${CRASH_COUNT} crashes in fuzzer01${NC}"
    
    if [ "${CRASH_COUNT}" -gt 0 ]; then
        echo -e "${YELLOW}[*] Copying crashes to results directory...${NC}"
        mkdir -p "../results/afl_crashes"
        find "${OUTPUT_DIR}" -path "*/crashes/*" -type f ! -name "README.txt" -exec cp {} "../results/afl_crashes/" \;
        echo -e "${YELLOW}[*] Crashes copied to: $(realpath "../results/afl_crashes")${NC}"
    fi
fi

echo -e "\n${GREEN}[*] Fuzzing session complete!${NC}"
