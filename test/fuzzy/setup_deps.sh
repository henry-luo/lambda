#!/bin/bash
set -e

echo "[+] Setting up fuzzing environment..."

# Install AFL++
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "[+] Installing AFL++ via Homebrew..."
    brew install afl-fuzz
    
    # Check if LLVM is installed
    if ! command -v clang &> /dev/null; then
        echo "[!] LLVM/clang not found. Installing..."
        brew install llvm
        echo 'export PATH="/usr/local/opt/llvm/bin:$PATH"' >> ~/.zshrc
        export PATH="/usr/local/opt/llvm/bin:$PATH"
    fi
    
elif [[ -f /etc/debian_version ]]; then
    echo "[+] Installing AFL++ via apt..."
    sudo apt-get update
    sudo apt-get install -y afl++ clang
    
else
    echo "[!] Unsupported OS. Please install AFL++ and clang manually."
    exit 1
fi

# Install Python dependencies
echo "[+] Installing Python dependencies..."
pip3 install --user hypothesis pyyaml

echo -e "\n[+] Setup complete!"
echo "[+] Next steps:"
echo "  1. Generate test corpus: python3 test/fuzzy/scripts/generate_corpus.py"
echo "  2. Run basic fuzzer: python3 test/fuzzy/scripts/basic_fuzz.py"
echo "  3. Or run AFL++ fuzzer: ./test/fuzzy/scripts/afl_wrapper.sh"
