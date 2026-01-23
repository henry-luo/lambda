# Lambda Script Fuzz Testing Plan

## 1. Setup and Dependencies

### 1.1 Dependencies Installation Script
Create `./test/fuzzy/setup_deps.sh`:
```bash
#!/bin/bash
set -e

# Install system dependencies
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    brew install afl-fuzz python3
elif [[ -f /etc/debian_version ]]; then
    # Debian/Ubuntu
    sudo apt-get update
    sudo apt-get install -y afl++ python3 python3-pip
else
    echo "Unsupported OS. Please install AFL++ and Python 3 manually."
    exit 1
fi

# Install Python dependencies
pip3 install --user hypothesis pyyaml

echo "Dependencies installed successfully!"
```

## 2. Fuzz Testing Structure

### 2.1 Directory Structure
```
test/
  fuzzy/
    corpus/           # Initial test cases
    scripts/          # Fuzzing scripts
    results/          # Crash reports and findings
    grammar_aware/    # Grammar-aware fuzzing
    unit/             # Unit tests with fuzzing
```

### 2.2 Grammar-Aware Fuzzing
Create `./test/fuzzy/grammar_aware/lambda_grammar.py`:
```python
import random
import string
from typing import List, Dict, Any

class LambdaGrammarFuzzer:
    def __init__(self):
        self.vars = ['x', 'y', 'z', 'a', 'b', 'c']
        self.funcs = ['sin', 'cos', 'tan', 'sqrt', 'log']
        
    def gen_number(self) -> str:
        if random.random() < 0.3:
            return str(random.randint(-100, 100))
        return f"{random.uniform(-100, 100):.4f}"
    
    def gen_var(self) -> str:
        return random.choice(self.vars)
    
    def gen_expr(self, depth: int = 0) -> str:
        if depth > 3:  # Limit recursion depth
            return self.gen_number() if random.random() < 0.7 else self.gen_var()
            
        expr_type = random.choices(
            ['number', 'var', 'binary', 'func', 'paren'],
            weights=[0.2, 0.2, 0.4, 0.1, 0.1]
        )[0]
        
        if expr_type == 'number':
            return self.gen_number()
        elif expr_type == 'var':
            return self.gen_var()
        elif expr_type == 'binary':
            op = random.choice(['+', '-', '*', '/', '^'])
            left = self.gen_expr(depth + 1)
            right = self.gen_expr(depth + 1)
            return f"{left} {op} {right}"
        elif expr_type == 'func':
            func = random.choice(self.funcs)
            arg = self.gen_expr(depth + 1)
            return f"{func}({arg})"
        else:  # paren
            return f"({self.gen_expr(depth + 1)})"
    
    def generate(self) -> str:
        # Start with a simple expression
        return self.gen_expr(0)
```

## 3. Fuzzing Scripts

### 3.1 Basic Fuzzer
Create `./test/fuzzy/scripts/basic_fuzz.py`:
```python
#!/usr/bin/env python3
import os
import random
import subprocess
import sys
from pathlib import Path

LAMBDA_BIN = "../../build/lambda"  # Update this path
TIMEOUT = 5  # seconds

def run_lambda(script: str) -> bool:
    try:
        result = subprocess.run(
            [LAMBDA_BIN],
            input=script.encode('utf-8'),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=TIMEOUT
        )
        return result.returncode == 0
    except subprocess.TimeoutExpired:
        print("Timeout!")
        return False
    except Exception as e:
        print(f"Error: {e}")
        return False

def save_crash(script: str, output: str):
    crash_dir = Path("../results/crashes")
    crash_dir.mkdir(parents=True, exist_ok=True)
    
    crash_id = f"crash_{len(list(crash_dir.glob('*'))) + 1}"
    (crash_dir / f"{crash_id}.ls").write_text(script)
    (crash_dir / f"{crash_id}.log").write_text(output)

def main():
    from lambda_grammar import LambdaGrammarFuzzer
    
    fuzzer = LambdaGrammarFuzzer()
    iteration = 0
    
    try:
        while True:
            script = fuzzer.generate()
            success = run_lambda(script)
            
            if not success:
                print(f"Potential crash found!\n{script}")
                save_crash(script, "Check logs")
                
            iteration += 1
            if iteration % 100 == 0:
                print(f"Iteration: {iteration}")
                
    except KeyboardInterrupt:
        print("\nFuzzing stopped by user")

if __name__ == "__main__":
    main()
```

### 3.2 AFL++ Wrapper
Create `./test/fuzzy/scripts/afl_wrapper.sh`:
```bash
#!/bin/bash
set -e

# Build Lambda with AFL instrumentation
mkdir -p ../../build_afl
cd ../../build_afl
CC=afl-gcc CXX=afl-g++ cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)

# Prepare corpus
mkdir -p corpus
cp ../../test/lambda/*.ls corpus/ 2>/dev/null || true

# Run AFL
afl-fuzz -i corpus/ -o findings -- ./lambda @@
```

## 4. Test Case Generation

### 4.1 Generate Initial Corpus
Create `./test/fuzzy/scripts/generate_corpus.py`:
```python
#!/usr/bin/env python3
import random
from pathlib import Path
from lambda_grammar import LambdaGrammarFuzzer

CORPUS_DIR = Path("../corpus")
CORPUS_SIZE = 1000

def generate_corpus():
    fuzzer = LambdaGrammarFuzzer()
    CORPUS_DIR.mkdir(exist_ok=True)
    
    for i in range(CORPUS_SIZE):
        script = fuzzer.generate()
        (CORPUS_DIR / f"test_{i:04d}.ls").write_text(script)

if __name__ == "__main__":
    generate_corpus()
    print(f"Generated {CORPUS_SIZE} test cases in {CORPUS_DIR}")
```

## 5. Running the Fuzzer

### 5.1 Setup and Run
```bash
# Make scripts executable
chmod +x test/fuzzy/scripts/*.sh
chmod +x test/fuzzy/scripts/*.py

# Install dependencies
./test/fuzzy/setup_deps.sh

# Generate initial corpus
python3 test/fuzzy/scripts/generate_corpus.py

# Run basic fuzzer
python3 test/fuzzy/scripts/basic_fuzz.py

# Or run AFL++ fuzzer
./test/fuzzy/scripts/afl_wrapper.sh
```

## 6. Crash Analysis

### 6.1 Analyze Crashes
Create `./test/fuzzy/scripts/analyze_crashes.py`:
```python
#!/usr/bin/env python3
from pathlib import Path
import subprocess

CRASH_DIR = Path("../results/crashes")
LAMBDA_BIN = "../../build/lambda"

def analyze_crash(crash_file: Path):
    print(f"\nAnalyzing {crash_file.name}...")
    script = crash_file.read_text()
    
    try:
        result = subprocess.run(
            [LAMBDA_BIN],
            input=script.encode('utf-8'),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=5
        )
        
        print("=== Script ===")
        print(script)
        print("\n=== STDOUT ===")
        print(result.stdout.decode('utf-8', 'replace'))
        print("\n=== STDERR ===")
        print(result.stderr.decode('utf-8', 'replace'))
        
    except Exception as e:
        print(f"Error analyzing {crash_file.name}: {e}")

def main():
    crash_files = list(CRASH_DIR.glob("*.ls"))
    if not crash_files:
        print("No crash files found!")
        return
        
    for crash_file in crash_files:
        analyze_crash(crash_file)

if __name__ == "__main__":
    main()
```

## 7. Continuous Integration

### 7.1 GitHub Actions Workflow
Create `.github/workflows/fuzz.yml`:
```yaml
name: Fuzz Testing

on: [push, pull_request]

jobs:
  fuzz:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v2
    
    - name: Install dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y afl++
        
    - name: Build with AFL instrumentation
      run: |
        mkdir -p build_afl
        cd build_afl
        CC=afl-gcc CXX=afl-g++ cmake .. -DCMAKE_BUILD_TYPE=Debug
        make -j$(nproc)
        
    - name: Run fuzzer
      run: |
        mkdir -p test/fuzzy/corpus
        cp test/lambda/*.ls test/fuzzy/corpus/ 2>/dev/null || true
        
        # Run AFL for 5 minutes
        timeout 5m afl-fuzz -i test/fuzzy/corpus/ -o fuzz_output -- ./build_afl/lambda @@
      continue-on-error: true
      
    - name: Upload crashes
      if: failure()
      uses: actions/upload-artifact@v2
      with:
        name: fuzz-crashes
        path: fuzz_output/crashes/
```

## 8. Next Steps

1. **Enhance Grammar Coverage**:
   - Add more complex language constructs
   - Include edge cases from existing test suite
   
2. **Improve Crash Analysis**:
   - Automate crash deduplication
   - Add stack trace analysis
   
3. **Performance Optimization**:
   - Add performance benchmarks
   - Monitor memory usage during fuzzing

4. **Integration Testing**:
   - Add end-to-end test cases
   - Test with real-world Lambda scripts

## 9. References

- [AFL++ Documentation](https://aflplus.plus/)
- [Hypothesis Documentation](https://hypothesis.readthedocs.io/)
- [Tree-sitter Documentation](https://tree-sitter.github.io/tree-sitter/)
