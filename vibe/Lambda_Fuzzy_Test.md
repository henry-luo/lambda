# Lambda Script Fuzzy Testing Proposal

## Overview

This proposal outlines a comprehensive fuzzy testing strategy for Lambda Script to validate robustness across the entire compilation and execution pipeline:

1. **Syntax Parsing** (Tree-sitter grammar → CST)
2. **AST Building** (CST → typed AST)
3. **Transpilation** (AST → MIR code)
4. **JIT Compilation** (MIR → native code)
5. **Runtime Execution** (native execution with memory management)

## Goals

- **Crash resistance**: No input should cause segfaults or undefined behavior
- **Error recovery**: Invalid inputs should produce meaningful error messages
- **Memory safety**: No leaks, use-after-free, or buffer overflows
- **Determinism**: Same input always produces same output (or consistent error)

---

## Phase 1: Grammar-Based Fuzzing (Syntax Parsing)

### 1.1 Random Token Generation

Generate random sequences of valid Lambda tokens:

```cpp
// Token categories to fuzz
enum TokenCategory {
    KEYWORD,      // fn, let, if, else, for, in, while, var, return, break, continue, pn, pub, type, import
    OPERATOR,     // + - * / ^ % == != < > <= >= and or not is in to | & !
    LITERAL,      // integers, floats, strings, symbols, binary, datetime
    DELIMITER,    // ( ) [ ] { } < > ; : , . => ->
    IDENTIFIER,   // valid identifiers
    WHITESPACE,   // spaces, tabs, newlines
    COMMENT,      // // line comments
};

std::string generate_random_token_sequence(int length);
```

### 1.2 Grammar-Aware Mutation

Mutate valid Lambda programs with targeted changes:

```cpp
struct Mutation {
    enum Type {
        DELETE_TOKEN,       // Remove a random token
        INSERT_TOKEN,       // Insert random token at random position
        SWAP_TOKENS,        // Swap two adjacent tokens
        DUPLICATE_TOKEN,    // Duplicate a token
        REPLACE_KEYWORD,    // Replace keyword with another keyword
        CORRUPT_STRING,     // Add invalid UTF-8 or unclosed quotes
        CORRUPT_NUMBER,     // Invalid number formats (multiple dots, invalid exponents)
        NEST_DEEPLY,        // Create deeply nested structures
        UNBALANCED_PARENS,  // Unbalanced brackets/parens/braces
    };
};

std::string mutate_program(const std::string& valid_program, Mutation::Type type);
```

### 1.3 Edge Case Corpus

Test specific parser edge cases:

```lambda
// Empty and whitespace
""
"   "
"\n\n\n"

// Maximum nesting
((((((((((((((((((((x))))))))))))))))))))
[[[[[[[[[[[[[[[[[[[[]]]]]]]]]]]]]]]]]]]]
{{{{{{{{{{{{{{{{{{{{}}}}}}}}}}}}}}}}}}}}

// Extremely long identifiers
let aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa = 1

// Unicode edge cases
let 你好 = "世界"
let α = 3.14159
"string with \u{10FFFF} max codepoint"

// Number edge cases
0
-0
00000000000000000001
999999999999999999999999999999999999999n
1e999
1e-999
inf
-inf
nan

// String edge cases
""
"\"escaped\""
"multi
line
string"
"\x00\x01\x02"

// Binary edge cases
b''
b'\x'
b'\xGG'
b'\64invalid'

// DateTime edge cases
t''
t'9999-99-99'
t'2025-01-01T99:99:99Z'
```

---

## Phase 2: AST Building Fuzzing

### 2.1 Valid Parse, Invalid Semantics

Programs that parse but have semantic issues:

```lambda
// Type mismatches
let x: int = "string"
let y: string = 42

// Undefined references
let a = undefined_var
fn f() => unknown_func()

// Duplicate definitions
let x = 1; let x = 2;
fn f() => 1; fn f() => 2;

// Invalid function signatures
fn f(x, x) => x           // Duplicate params
fn f(..., y) => y         // Param after variadic
fn f(x = 1, y) => x + y   // Required after default

// Closure edge cases
fn outer() {
    let captured = 42
    fn inner() => captured + undefined
    inner
}

// Recursive depth
fn f(n) => if (n > 0) f(f(f(f(n-1)))) else 0
```

### 2.2 Type System Stress

```lambda
// Complex union types
type T = int | string | float | bool | null | error | (int | string) | [int | string]

// Recursive types
type Node = {value: int, next: Node?}

// Function types
type F = (int, string) => (bool) => (float) => int

// Deeply nested optional
type Deep = int??????????
```

---

## Phase 3: Transpiler Fuzzing (AST → MIR)

### 3.1 Control Flow Complexity

```lambda
// Deeply nested control flow
fn complex(n) {
    if (n > 0) {
        for (i in 1 to n) {
            if (i % 2 == 0) {
                for (j in 1 to i) {
                    if (j % 3 == 0) {
                        n
                    } else {
                        j
                    }
                }
            } else {
                i
            }
        }
    } else {
        0
    }
}

// Many variables (register pressure)
fn many_vars() {
    let a=1, b=2, c=3, d=4, e=5, f=6, g=7, h=8;
    let i=9, j=10, k=11, l=12, m=13, n=14, o=15, p=16;
    a+b+c+d+e+f+g+h+i+j+k+l+m+n+o+p
}

// Closure capture complexity
fn outer(a, b, c) {
    fn mid(d, e) {
        fn inner(f) => a + b + c + d + e + f
        inner
    }
    mid
}
```

### 3.2 Procedural Constructs

```lambda
// While loop edge cases
pn infinite() { while(true) { } }
pn empty_while() { while(false) { } }
pn break_continue() {
    var i = 0;
    while (i < 100) {
        i = i + 1;
        if (i == 50) continue;
        if (i == 75) break;
    }
    i
}

// Nested loops with break/continue
pn nested_break() {
    var result = 0;
    while (true) {
        while (true) {
            result = result + 1;
            if (result > 10) break;
        }
        if (result > 50) break;
    }
    result
}

// Early return
pn early_return(n) {
    if (n < 0) return -1;
    if (n == 0) return 0;
    var x = 1;
    while (x < n) {
        x = x * 2;
        if (x > 1000) return 1000;
    }
    return x;
}
```

---

## Phase 4: MIR JIT Compilation Fuzzing

### 4.1 Numeric Edge Cases

```lambda
// Integer overflow
let max_int = 2147483647
max_int + 1
max_int * max_int

// Float precision
0.1 + 0.2
1e308 * 2
1e-308 / 1e308

// Division edge cases
1 / 0
0 / 0
(-1) _/ 0
1.0 / 0.0

// Decimal arithmetic
123456789012345678901234567890n + 1n
999999999999999999999999999999n * 999999999999999999999999999999n
```

### 4.2 Memory Allocation Stress

```lambda
// Large array creation
let big = fill(1000000, 0)
let huge = range(0, 10000000, 1)

// Deep recursion
fn deep(n) => if (n > 0) deep(n - 1) else 0
deep(100000)

// Many small allocations
fn alloc_many(n) {
    for (i in 1 to n) {
        let temp = [i, i+1, i+2]
        sum(temp)
    }
}
alloc_many(100000)

// String concatenation
fn concat_many(n) {
    var s = "";
    for (i in 1 to n) {
        s = s + "x"
    }
    s
}
```

---

## Phase 5: Runtime Execution Fuzzing

### 5.1 Reference Counting Stress

```lambda
// Circular-like patterns (should not create actual cycles due to immutability)
let a = [1, 2, 3]
let b = [a, a, a]
let c = [b, b, b]

// Rapid create/destroy
fn churn(n) {
    for (i in 1 to n) {
        let temp = {x: i, y: [i, i+1], z: "string"}
        temp.x
    }
}

// Closure reference retention
fn make_closures(n) {
    for (i in 1 to n) {
        let captured = fill(100, i)
        fn f() => sum(captured)
        f
    }
}
```

### 5.2 System Function Fuzzing

```lambda
// Input parsing with malformed data
input("nonexistent.json", 'json')
input("/dev/null", 'json')

// Format edge cases
format(null, 'json')
format(inf, 'json')
format(nan, 'yaml')
format({recursive: {deep: {nesting: 100}}}, 'json')

// Collection function edge cases
slice([], 0, 100)
slice([1,2,3], -1, 10)
slice([1,2,3], 5, 2)
take([], 1000)
drop([1], 1000)
sort([])
reverse([])
unique([])
zip([], [1,2,3])
zip([1,2,3], [])

// Vector function edge cases
dot([], [])
dot([1], [1,2])
norm([])
cumsum([])
argmin([])
argmax([])

// Statistical edge cases
sum([])
avg([])
median([])
variance([])
deviation([])
quantile([], 0.5)
quantile([1,2,3], -1)
quantile([1,2,3], 2)
```

---

## Implementation Plan

### Test Harness Architecture

```cpp
// test/fuzzy/lambda_fuzzer.cpp

class LambdaFuzzer {
public:
    enum Stage {
        PARSE,
        BUILD_AST,
        TRANSPILE,
        JIT_COMPILE,
        EXECUTE
    };
    
    struct Result {
        Stage failed_stage;
        bool crashed;
        bool timeout;
        std::string error_message;
        double execution_time;
        size_t memory_peak;
    };
    
    Result fuzz(const std::string& input, int timeout_ms = 5000);
    
private:
    bool parse(const std::string& input);
    bool build_ast();
    bool transpile();
    bool jit_compile();
    bool execute();
};

// Differential testing: compare interpreter vs JIT results
class DifferentialFuzzer {
public:
    bool compare(const std::string& input) {
        auto interp_result = run_interpreter(input);
        auto jit_result = run_jit(input);
        return results_match(interp_result, jit_result);
    }
};
```

### Corpus Management

```
test/fuzzy/
├── corpus/
│   ├── valid/           # Valid Lambda programs (seeds)
│   ├── edge_cases/      # Known edge cases
│   ├── crashes/         # Inputs that caused crashes (auto-saved)
│   └── slow/            # Inputs that caused timeouts
├── generators/
│   ├── grammar_gen.cpp  # Grammar-based generation
│   ├── mutator.cpp      # Mutation engine
│   └── minimizer.cpp    # Crash case minimizer
└── harness/
    ├── lambda_fuzzer.cpp
    └── differential_fuzzer.cpp
```

### Build & Run

Add to `Makefile`:

```makefile
# Fuzzy testing
test-fuzzy: build-fuzzy
	@echo "Running fuzzy tests..."
	./build/bin/lambda_fuzzer --duration=1h --corpus=test/fuzzy/corpus

build-fuzzy: build
	@echo "Building fuzzer..."
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined \
		test/fuzzy/harness/lambda_fuzzer.cpp \
		test/fuzzy/generators/grammar_gen.cpp \
		test/fuzzy/generators/mutator.cpp \
		-o build/bin/lambda_fuzzer \
		-Lbuild/lib -llambda
```

**Usage:**
```bash
make test-fuzzy                    # Run 1-hour fuzzy test
make test-fuzzy DURATION=4h        # Run 4-hour fuzzy test
make build-fuzzy                   # Build fuzzer only
```

---

## Success Metrics

| Metric | Target |
|--------|--------|
| Crash rate | 0% (no crashes on any input) |
| Timeout rate | < 0.1% (with 5s timeout) |
| Memory leak rate | 0% (no leaks detected by ASan) |
| Differential test pass rate | 100% (interpreter == JIT) |
| Code coverage | > 80% of parser, AST, transpiler code |
| Edge case coverage | 100% of documented edge cases |

---

## Timeline

| Phase | Duration | Deliverables |
|-------|----------|--------------|
| 1. Infrastructure | 1 week | Fuzzer harness, corpus management |
| 2. Grammar fuzzing | 1 week | Token generator, mutation engine |
| 3. Semantic fuzzing | 1 week | AST/type system test cases |
| 4. Runtime fuzzing | 1 week | Memory/execution stress tests |
| 5. Bug fixes | Ongoing | Fix discovered issues |

---

## Tools & Dependencies

- **libFuzzer** or **AFL++**: Coverage-guided fuzzing engine
- **AddressSanitizer (ASan)**: Memory error detection
- **UndefinedBehaviorSanitizer (UBSan)**: UB detection
- **Valgrind**: Additional memory analysis
- **gcov/llvm-cov**: Code coverage measurement

## References

- [libFuzzer Documentation](https://llvm.org/docs/LibFuzzer.html)
- [AFL++ GitHub](https://github.com/AFLplusplus/AFLplusplus)
- [Google OSS-Fuzz](https://github.com/google/oss-fuzz)
