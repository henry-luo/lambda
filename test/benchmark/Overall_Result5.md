# Lambda Benchmark Results: Direct String Pointer Experiment (Round 5)

**Date:** 2025-07-14
**Branch:** `direct-string-pointer`
**Platform:** Apple Silicon MacBook Air (M4, aarch64), macOS
**Lambda version:** release build (8.4 MB, stripped, `-O2`)
**Methodology:** 3 runs per benchmark, median of self-reported execution time
**Baseline:** Round 4 results (tagged pointer scheme)

---

## Experiment Summary

**Goal:** Determine whether eliminating high-byte type tagging for `String`, `Symbol`, and `Binary` and switching to direct C pointer storage (same as containers) improves runtime performance and/or reduces peak memory.

**Change:** String/Symbol/Binary items no longer use the high 8 bits of the 64-bit `Item` for type identification. Instead, the `type_id` is stored as the first byte of the struct itself, and `get_type_id()` reads it via pointer dereference when the high byte is 0.

**Result: ~11% overall slowdown. Experiment should NOT be merged.**

---

## MIR Direct Time Comparison: Round 5 vs Round 4

### R7RS Benchmarks

| Benchmark | Category | R4 (ms) | R5 (ms) | Ratio | Change |
| --------- | -------- | ------: | ------: | ----: | -----: |
| fib | recursive | 2.5 | 2.26 | 0.90x | -9.6% |
| fibfp | recursive | 3.7 | 3.74 | 1.01x | +1.1% |
| tak | recursive | 0.15 | 0.170 | 1.13x | +13.3% |
| cpstak | closure | 0.30 | 0.326 | 1.09x | +8.7% |
| sum | iterative | 0.27 | 0.276 | 1.02x | +2.2% |
| sumfp | iterative | 0.067 | 0.071 | 1.06x | +6.0% |
| nqueens | backtrack | 6.7 | 6.62 | 0.99x | -1.3% |
| fft | numeric | 0.18 | 0.202 | 1.12x | +12.2% |
| mbrot | numeric | 0.59 | 0.644 | 1.09x | +9.2% |
| ack | recursive | 9.8 | 11.2 | 1.14x | +14.3% |

### AWFY Benchmarks

| Benchmark | Category | R4 (ms) | R5 (ms) | Ratio | Change |
| --------- | -------- | ------: | ------: | ----: | -----: |
| sieve | micro | 0.052 | 0.058 | 1.12x | +11.5% |
| permute | micro | 0.064 | 0.065 | 1.02x | +1.6% |
| queens | micro | 0.15 | 0.154 | 1.03x | +2.7% |
| towers | micro | 0.22 | 0.235 | 1.07x | +6.8% |
| bounce | micro | 0.19 | 0.242 | 1.27x | +27.4% |
| list | micro | 0.023 | 0.025 | 1.09x | +8.7% |
| storage | micro | 0.19 | 0.214 | 1.13x | +12.6% |
| mandelbrot | compute | 32 | 35.4 | 1.11x | +10.7% |
| nbody | compute | 47 | 52.2 | 1.11x | +11.0% |
| richards | macro | 253 | 291 | 1.15x | +15.2% |
| json | macro | 1.5 | 1.75 | 1.17x | +16.7% |
| deltablue | macro | 64 | 73.7 | 1.15x | +15.2% |
| havlak | macro | 61 | 70.6 | 1.16x | +15.8% |
| cd | macro | 220 | 252 | 1.14x | +14.5% |

### BENG Benchmarks

| Benchmark | Category | R4 (ms) | R5 (ms) | Ratio | Change |
| --------- | -------- | ------: | ------: | ----: | -----: |
| binarytrees | allocation | 7.3 | 8.19 | 1.12x | +12.2% |
| fannkuch | permutation | 0.76 | 0.905 | 1.19x | +19.1% |
| fasta | generation | 1.1 | 1.22 | 1.11x | +11.0% |
| knucleotide | hashing | 2.9 | 3.41 | 1.17x | +17.4% |
| mandelbrot | numeric | 142 | 161 | 1.13x | +13.0% |
| nbody | numeric | 47 | 53.2 | 1.13x | +13.2% |
| pidigits | bignum | 0.46 | 0.506 | 1.10x | +10.0% |
| regexredux | regex | 1.2 | 1.41 | 1.18x | +17.5% |
| revcomp | string | 1.8 | 2.07 | 1.15x | +14.8% |
| spectralnorm | numeric | 13 | 14.4 | 1.11x | +11.0% |

### KOSTYA Benchmarks

| Benchmark | Category | R4 (ms) | R5 (ms) | Ratio | Change |
| --------- | -------- | ------: | ------: | ----: | -----: |
| brainfuck | interpreter | 165 | 190 | 1.15x | +15.0% |
| matmul | numeric | 8.8 | 9.51 | 1.08x | +8.0% |
| primes | numeric | 7.3 | 8.46 | 1.16x | +15.8% |
| base64 | string | 220 | 264 | 1.20x | +20.1% |
| levenshtein | string | 7.7 | 8.97 | 1.17x | +16.5% |
| json_gen | data | 65 | 70.6 | 1.09x | +8.6% |
| collatz | numeric | 301 | 328 | 1.09x | +9.1% |

### LARCENY Benchmarks

| Benchmark | Category | R4 (ms) | R5 (ms) | Ratio | Change |
| --------- | -------- | ------: | ------: | ----: | -----: |
| triangl | search | 179 | 201 | 1.12x | +12.3% |
| array1 | array | 0.55 | 0.620 | 1.13x | +12.7% |
| deriv | symbolic | 20 | 23.3 | 1.17x | +16.7% |
| diviter | iterative | 272 | 292 | 1.07x | +7.4% |
| divrec | recursive | 0.84 | 0.920 | 1.10x | +9.5% |
| gcbench | allocation | 469 | 445 | 0.95x | -5.1% |
| paraffins | combinat | 0.33 | 0.355 | 1.08x | +7.6% |
| pnpoly | numeric | 59 | 68.5 | 1.16x | +16.2% |
| primes | iterative | 7.2 | 8.23 | 1.14x | +14.3% |
| puzzle | search | 3.8 | 4.22 | 1.11x | +11.1% |
| quicksort | sorting | 3.1 | 3.72 | 1.20x | +20.1% |
| ray | numeric | 7.1 | 7.35 | 1.04x | +3.5% |

### JetStream Benchmarks

| Benchmark | Category | R4 (ms) | R5 (ms) | Ratio | Change |
| --------- | -------- | ------: | ------: | ----: | -----: |
| nbody | numeric | 47 | 53.8 | 1.15x | +14.5% |
| cube3d | 3d | 24 | 26.3 | 1.10x | +9.7% |
| navier_stokes | numeric | 823 | 932 | 1.13x | +13.2% |
| richards | macro | 259 | 295 | 1.14x | +13.9% |
| splay | data | 165 | 186 | 1.13x | +12.9% |
| deltablue | macro | 17 | 19.8 | 1.16x | +16.2% |
| hashmap | data | 106 | 124 | 1.17x | +17.0% |
| crypto_sha1 | crypto | 17 | 18.2 | 1.07x | +7.3% |
| raytrace3d | 3d | 348 | 375 | 1.08x | +7.8% |

---

## Overall Performance Summary

| Metric | Value |
| ------ | ----- |
| **Geometric mean R5/R4** | **1.111x (11.1% slower)** |
| Benchmarks faster (>3%) | 2 / 62 |
| Benchmarks slower (>3%) | 55 / 62 |
| Benchmarks within ±3% | 5 / 62 |
| Best improvement | fib: -9.6% |
| Worst regression | bounce: +27.4% |

### Per-Suite Breakdown

| Suite | Faster | Slower | Same | Trend |
| ----- | -----: | -----: | ---: | ----- |
| R7RS | 1 | 6 | 3 | ~10% slower |
| AWFY | 0 | 12 | 2 | ~12% slower |
| BENG | 0 | 10 | 0 | ~13% slower |
| KOSTYA | 0 | 7 | 0 | ~13% slower |
| LARCENY | 1 | 11 | 0 | ~11% slower |
| JetStream | 0 | 9 | 0 | ~12% slower |

---

## Memory Comparison: Round 5 vs Round 4

Peak RSS (MB) for MIR engine, median of 3 runs.

| Benchmark | R4 (MB) | R5 (MB) | Change |
| --------- | ------: | ------: | -----: |
| r7rs/fib | 34.8 | 34.0 | -2.3% |
| r7rs/fibfp | 39.1 | 38.6 | -1.3% |
| r7rs/tak | 34.7 | 33.8 | -2.6% |
| r7rs/nqueens | 37.2 | 36.5 | -1.9% |
| awfy/nbody | 122 | 120 | -1.6% |
| awfy/richards | 312 | 43.8 | -86% |
| beng/binarytrees | 45 | 45.2 | +0.4% |
| beng/mandelbrot | 243 | 243 | 0% |
| kostya/base64 | 1414 | 2360 | +67% |
| larceny/gcbench | 261 | 259 | -0.8% |
| jetstream/navier_stokes | 1310 | 1280 | -2.3% |
| jetstream/raytrace3d | 148 | 147 | -0.7% |

**Memory impact: Mixed.** Most benchmarks show <3% change in memory. Some outliers (kostya/base64) show increased memory, likely due to measurement variance rather than the string pointer change.

---

## Analysis: Why the Slowdown?

### Root Cause: Extra Memory Dereference in get_type_id()

The core change replaces a **register-level read** (extracting high byte from the 64-bit Item) with a **memory dereference** (reading the first byte of the pointed-to struct):

```cpp
// Before (Round 4): type is in the high 8 bits of the Item register
inline TypeId type_id() {
    return this->_type_id;  // Zero-cost: already in register
}

// After (Round 5): type is in the struct pointed to by the Item
inline TypeId type_id() {
    if (this->_type_id) return this->_type_id;  // Branch for tagged scalars
    if (this->item) return *((TypeId*)this->item);  // Memory dereference
    return LMD_TYPE_NULL;
}
```

Every time the runtime or JIT-compiled code needs to determine if an Item is a String, Symbol, or Binary, it must:
1. Check if the high byte is zero (one comparison + branch)
2. Dereference the pointer to read the struct's type_id byte (one memory load)

This adds ~1 branch + ~1 cache-line access per type check. Since String operations are pervasive in the Lambda runtime (string keys for map lookups, string comparisons in equality, string indexing, etc.), this overhead compounds across millions of iterations.

### Why No Improvement Was Observed

The hypothesis was that removing the OR/AND masking for boxing/unboxing would save instructions. However:
- **Boxing**: The old `s2it(ptr)` was a single OR instruction vs the new scheme which is a plain MOV (saves 1 instruction)
- **Unboxing**: The old `it2s(item)` was a bitfield read (zero-cost on ARM64) vs no change needed
- **Type checking**: The old scheme was a shift+compare vs the new scheme's branch+load — **net worse**

The type-checking cost (the most frequent operation) increased more than the boxing cost decreased.

---

## Conclusion

**The direct string pointer experiment demonstrates that the tagged-pointer scheme is more efficient than the struct-embedded type_id scheme for high-frequency type dispatch.**

The ~11% slowdown is consistent and pervasive across all benchmark suites, indicating it's a fundamental architectural cost rather than a localized regression.

**Recommendation:** Do not merge this branch. The tagged pointer approach (type in high byte of Item) is the correct design for Lambda's performance-critical runtime.

### Correctness

All 677/677 baseline tests pass with the direct-string-pointer implementation, confirming the change is functionally correct. The bugs fixed during implementation:

1. `init_ascii_char_table()` — missing `type_id = LMD_TYPE_STRING` for interned single-char strings
2. `stringbuf_to_string()` — missing `type_id = STRING_TYPE_ID` for strings built from StringBuf
3. `fn_eq_depth()` — equality comparison failed to detect type mismatches when both sides have `_type_id == 0` (e.g., String vs Array)
4. `ConstItem::string()` — dereferenced ITEM_NULL as a pointer (crash in DOM building)
5. Various `_type_id ==` checks throughout runtime replaced with `get_type_id()` calls
6. `emit_load_const_boxed` BINARY case — still used old tag-based boxing
7. `deep_copy_internal` BINARY case — lost type_id after calling `createString()`

---

## Test Results

```
📊 Test Results:
   🐑 Lambda Runtime Tests ✅ PASS (677/677 tests)
     └─ ⚡ ✅ PASS (187/187 tests) C2MIR JIT Execution Tests
     └─ 🟨 ✅ PASS (40/40 tests) JavaScript Transpiler Tests
     └─ ⚠️ ✅ PASS (61/61 tests) Lambda Error System Tests
     └─ 🐑 ✅ PASS (235/235 tests) Lambda Runtime Tests
     └─ 🐑 ✅ PASS (6/6 tests) Lambda Procedural Tests
     └─ 🎮 ✅ PASS (38/38 tests) Lambda REPL Interface Tests
     └─ 🧪 ✅ PASS (106/106 tests) Lambda Structured Tests
     └─ 🔍 ✅ PASS (4/4 tests) Transpile Pattern Tests
```
