# Lambda × Computer Language Benchmarks Game (BENG) Proposal

## 1. Overview

The [Computer Language Benchmarks Game](https://benchmarksgame-team.pages.debian.net/benchmarksgame/) (BENG) is the most widely recognized cross-language performance comparison suite. It measures toy-program performance for ~24 language implementations across 10 benchmark tasks covering numeric computation, memory allocation, string processing, regex matching, and I/O. Results are publicly available and frequently cited in language comparison discussions.

This proposal assesses the feasibility of porting the BENG suite to Lambda Script, identifies required language features, lays out an implementation plan, and describes how to run benchmarks and compile results.

### Why BENG?

| Property | BENG | AWFY | R7RS |
|----------|------|------|------|
| Public visibility | Very high (widely cited) | Academic niche | Scheme community |
| Workload diversity | High (numeric, string, I/O, regex, GC stress) | OOP patterns | Functional/recursive |
| Languages compared | ~24 (C, C++, Java, Python, Node.js, Rust, Go, ...) | ~10 | ~30 (Scheme only) |
| Benchmark count | 10 active | 14 | ~50 (we selected 10) |
| Focus | Real-world compute patterns | Abstraction overhead | Classic FP recursion |

BENG complements our existing benchmark suites:
- **AWFY** tests OOP-heavy patterns (objects, polymorphism, closures)
- **R7RS** tests classic functional/recursive patterns
- **BENG** tests numeric compute, memory allocation, string/collection processing, and arbitrary-precision arithmetic — filling the gap

### Reference Data

The BENG reference data, descriptions, and source code are located at:
- `./benchmarksgame/public/description/` — benchmark task descriptions (HTML)
- `./benchmarksgame/public/download/` — reference output files and source code archive
- `./benchmarksgame/public/data/data.csv` — performance measurements for ~24 languages
- `./benchmarksgame/public/data/ndata.csv` — measurements at all workload sizes

---

## 2. BENG Benchmark Summary

### 2.1 All 10 Active Benchmarks

The Benchmarks Game currently has 10 active benchmark tasks:

| # | Benchmark | Description | Key Operations | Workload (Perf) |
|---|-----------|-------------|----------------|-----------------|
| 1 | **fannkuch-redux** | Pancake flipping — count max flips over all N! permutations | Array reversal, permutation generation, tight loops | N=12 |
| 2 | **n-body** | 5-body gravitational simulation (Jovian planets) | Float math, sqrt, mutable arrays | 50,000,000 steps |
| 3 | **spectral-norm** | Eigenvalue via power method on infinite matrix | Float math, array dot products | N=5500 |
| 4 | **mandelbrot** | Generate Mandelbrot set as portable bitmap | Float math, bitwise packing, nested loops | 16000×16000 |
| 5 | **fasta** | Generate random DNA sequences with LCG PRNG | Modular arithmetic, cumulative probability lookup, string output | N=25,000,000 |
| 6 | **k-nucleotide** | Hash table k-mer frequency counting on DNA | Hash table update, string slicing, sorting | stdin input |
| 7 | **reverse-complement** | Read DNA FASTA, write reverse-complement | String reversal, character mapping, I/O | stdin input |
| 8 | **binary-trees** | Allocate/deallocate many binary trees | Recursive allocation, GC stress, tree traversal | depth=21 |
| 9 | **pidigits** | Streaming arbitrary-precision pi digits | Arbitrary-precision arithmetic | 10,000 digits |
| 10 | **regex-redux** | Match/replace DNA patterns with regex | Regex matching, string substitution | stdin input |

### 2.2 Benchmark Categories

| Category | Benchmarks | What It Tests |
|----------|-----------|---------------|
| **Pure Numeric** | fannkuch-redux, n-body, spectral-norm, mandelbrot | CPU-bound computation, loop optimization, float precision |
| **String/I/O** | fasta, reverse-complement | String construction, character lookup, I/O throughput |
| **Collection/Hash** | k-nucleotide | Hash table performance, string hashing |
| **Memory/GC** | binary-trees | Allocation rate, garbage collection efficiency |
| **Arbitrary Precision** | pidigits | Bignum arithmetic quality |
| **Regex** | regex-redux | Regex engine quality (deferred — see §12) |

---

## 3. Lambda Language Feature Assessment

### 3.1 Feature Matrix vs BENG Requirements

| BENG Requirement | Lambda Status | Detail |
|------------------|---------------|--------|
| Mutable arrays (indexed read/write) | **Supported** | `var arr = [...]; arr[i] = val` in `pn` functions |
| Float math (sqrt, sin, cos) | **Supported** | `sqrt`, `sin`, `cos`, `abs`, `floor`, `ceil`, `round`, `log`, `exp` |
| Bitwise operations | **Supported** | `shl`, `shr`, `band`, `bor`, `bxor`, `bnot` system functions |
| Integer arithmetic + modulo | **Supported** | `+`, `-`, `*`, `/`, `%` operators |
| While loops with mutation | **Supported** | `pn` functions: `var i = 0; while (i < n) { ... }` |
| String construction/concatenation | **Supported** | `++` operator, string interpolation |
| String split/join/replace | **Supported** | `split(str, sep)`, `str_join(arr, sep)`, `replace(str, old, new)` — all literal-only |
| String char access/iteration | **Supported** | `str[i]` for character access, `slice(str, i, j)` for substrings |
| Mutable hash table (dynamic keys) | **Supported** | `map()` constructor + `m.set(key, val)` for in-place insertion of arbitrary runtime keys |
| File input | **Supported** | `input(@path)` / `input(@path, 'text)` for reading files |
| stdin reading | **Not supported** | Lambda has no stdin reading; use file input as adaptation |
| stdout output | **Supported** | `print()` function |
| Arbitrary-precision arithmetic | **Supported** | `decimal` type with `N` suffix: unlimited precision, backed by libmpdec. Supports `+`, `-`, `*`, `/`, `%`, `**`, comparison, `abs`, `floor` |
| Regex matching/replacement | **Not supported** | No built-in regex library; `replace()` is literal-only. Lambda string patterns are type-level validators (full-match `is` check), not substring search/replace tools |
| Binary file output | **Partial** | `binary()` type exists; `output()` can write to files; PBM binary output needs verification |
| Recursive data structures | **Supported** | Maps with self-referencing fields |
| Nested functions / closures | **Supported** | `fn` / `pn` nesting, closure capture |
| Array construction | **Supported** | `fill(n, val)` creates arrays of n copies; `range(s, e, step)` for ranges |
| Sorting | **Supported** | `sort(arr)` / `sort(arr, comparator)` |
| Collection functions | **Supported** | `reverse`, `concat`, `take`, `drop`, `unique`, `slice`, `len` |

### 3.2 Feasibility Assessment per Benchmark

| # | Benchmark | Feasible? | Lambda Approach | Difficulty |
|---|-----------|-----------|-----------------|------------|
| 1 | **fannkuch-redux** | **Yes** | `pn` + mutable arrays + while loops | Easy |
| 2 | **n-body** | **Yes** | Adapt from existing AWFY `nbody.ls` | Easy |
| 3 | **spectral-norm** | **Yes** | `pn` + float arrays + nested loops | Easy |
| 4 | **mandelbrot** | **Yes** | `pn` + float math + bitwise packing (`band`, `bor`, `shl`) | Medium |
| 5 | **fasta** | **Yes** | `pn` + LCG PRNG + modular arithmetic + string output | Medium |
| 6 | **k-nucleotide** | **Yes** | `pn` + `map()` + `m.set(key, val)` for hash table; file input | Medium |
| 7 | **reverse-complement** | **Yes** | `pn` + `replace()` for complement mapping + `reverse()` + file input | Medium |
| 8 | **binary-trees** | **Yes** | `pn` + recursive maps `{left: ..., right: ...}` | Easy |
| 9 | **pidigits** | **Yes** | `pn` + `decimal` type with `N` suffix (unlimited precision) + `floor()` | Medium |
| 10 | **regex-redux** | **Deferred** | Requires regex-based substring search/count/replace (Lambda lacks this) | Blocked |

### 3.3 Adaptations Required

#### Adaptation 1: File Input Instead of stdin (k-nucleotide, reverse-complement)

Three BENG benchmarks officially read from stdin. Lambda has no stdin function.

**Solution**: Read from a file path passed as a CLI argument via `input(@path, 'text)`. The computation is identical; only the I/O source differs. This is a standard adaptation for languages without stdin.

#### Adaptation 2: Mandelbrot PBM Output

The mandelbrot benchmark outputs a Portable Bitmap (PBM P4) binary format — raw bytes packed 8 pixels per byte.

**Approach**: Use Lambda's bitwise functions (`shl`, `bor`, `band`) to pack 8 pixels per byte. Attempt binary output via `output()` with the `binary` type to write PBM P4 format. If binary file output is not supported, fall back to text-based PBM (P1 format) or adapted numeric output, documenting the adaptation.

#### Deferred: regex-redux (see §12)

The regex-redux benchmark requires regex-based substring search, counting, and replacement. Lambda's `replace()` is literal-only, and Lambda's string patterns are type-level validators (checked with `is` for full-string matching), not runtime substring search/replace tools. This benchmark is deferred until Lambda gains pattern-based string search/replace functions or a regex library.

---

## 4. Selected Benchmarks for Implementation

Based on the feasibility analysis, we implement **9 benchmarks**, deferring 1:

### 4.1 Round 1: Implementable Now (9 benchmarks)

| # | Benchmark | Category | Lambda Approach | Difficulty |
|---|-----------|----------|-----------------|------------|
| 1 | **fannkuch-redux** | Numeric/Array | `pn` with mutable array, permutation generation via while loops | Easy |
| 2 | **n-body** | Numeric/Float | `pn` with parallel arrays for body state, symplectic integrator (adapt AWFY) | Easy |
| 3 | **spectral-norm** | Numeric/Float | `pn` with arrays, matrix-vector multiply via $A_{ij} = 1/((i+j)(i+j+1)/2+i+1)$ | Easy |
| 4 | **mandelbrot** | Numeric/Float | `pn` with nested loops, bitwise packing via `shl`/`bor`, PBM output | Medium |
| 5 | **fasta** | String/Output | `pn` with LCG PRNG, cumulative probability lookup, string output | Medium |
| 6 | **k-nucleotide** | Collection/Hash | `pn` with `map()` + `m.set(key, val)` dynamic hash map, file input, `sort()` | Medium |
| 7 | **reverse-complement** | String/I/O | `pn` with file input, `replace()` character mapping, `reverse()` | Medium |
| 8 | **binary-trees** | Memory/GC | `pn` with recursive map-based tree nodes `{left: ..., right: ...}` | Easy |
| 9 | **pidigits** | Arb. Precision | `pn` with `decimal` `N`-suffix unlimited precision, spigot algorithm | Medium |

### 4.2 Deferred to Round 2 (1 benchmark)

| # | Benchmark | Blocking Issue | Unblock Condition |
|---|-----------|---------------|-------------------|
| 10 | **regex-redux** | No pattern-based string search/count/replace | Add regex search/replace functions or expose string patterns for substring operations |

---

## 5. Benchmark Specifications

### 5.1 fannkuch-redux

**Algorithm**: For each of the N! permutations of {1,...,N}:
1. Take the first element k, reverse the first k elements
2. Repeat until first element is 1
3. Count the flips; track max flips and running checksum

**Lambda implementation sketch**:
```lambda
pn fannkuch(n: int) -> int {
    var perm = fill(n, 0)
    var perm1 = fill(n, 0)
    var count = fill(n, 0)
    var max_flips = 0
    var checksum = 0

    // initialize perm1 = [0, 1, 2, ..., n-1]
    var i = 0
    while (i < n) { perm1[i] = i; i = i + 1 }

    var r = n
    var sign = 1
    while (true) {
        // copy perm1 to perm
        // count flips
        // update checksum: checksum = checksum + sign * flips
        // generate next permutation
        // ...
    }
    print(string(checksum) ++ "\n")
    print("Pfannkuchen(" ++ string(n) ++ ") = " ++ string(max_flips) ++ "\n")
    return max_flips
}
```

**Parameters**: N=7 (validation), N=12 (performance)
**Expected output** (N=7): `228\nPfannkuchen(7) = 16`

### 5.2 n-body

**Algorithm**: Symplectic integrator for 5-body (Sun + 4 Jovian planets) gravitational simulation.

**Lambda implementation**: Adapt from existing AWFY `test/benchmark/awfy/nbody.ls`. Changes for BENG format:
- Accept step count as parameter (N=50,000,000 for performance)
- Print energy before and after with 9 decimal places

**Parameters**: N=1000 (validation), N=50,000,000 (performance)
**Expected output** (N=1000): `-0.169075164\n-0.169087605`

### 5.3 spectral-norm

**Algorithm**: Compute the spectral norm of an infinite matrix A where $A_{ij} = \frac{1}{((i+j)(i+j+1)/2 + i + 1)}$, using the power method (repeated $A^T A v$ multiplication).

**Lambda implementation sketch**:
```lambda
pn A(i: int, j: int) -> float {
    return 1.0 / float((i + j) * (i + j + 1) / 2 + i + 1)
}

pn mul_Av(n: int, v, av) {
    var i = 0
    while (i < n) {
        var sum = 0.0
        var j = 0
        while (j < n) {
            sum = sum + A(i, j) * v[j]
            j = j + 1
        }
        av[i] = sum
        i = i + 1
    }
}

pn mul_Atv(n: int, v, atv) {
    var i = 0
    while (i < n) {
        var sum = 0.0
        var j = 0
        while (j < n) {
            sum = sum + A(j, i) * v[j]
            j = j + 1
        }
        atv[i] = sum
        i = i + 1
    }
}

pn mul_AtAv(n: int, v, out) {
    var tmp = fill(n, 0.0)
    mul_Av(n, v, tmp)
    mul_Atv(n, tmp, out)
}
```

**Parameters**: N=100 (validation), N=5500 (performance)
**Expected output** (N=100): `1.274219991`

### 5.4 mandelbrot

**Algorithm**: Compute the Mandelbrot set on an N×N grid over [-1.5-i, 0.5+i]. For each pixel, iterate $z_{n+1} = z_n^2 + c$ up to 50 times; if $|z| \leq 2$, pixel is in the set.

**Output**: PBM format. Pack 8 pixels into one byte using `shl`/`bor`. Write header `"P4\nN N\n"` followed by packed bytes.

**Lambda implementation highlights**:
- Bitwise packing: `byte = bor(shl(byte, 1), bit)` for each pixel
- Nested loops over (y, x) grid
- Float math for complex iteration

**Parameters**: N=200 (validation), N=16000 (performance)

### 5.5 fasta

**Algorithm**: Generate DNA sequences using:
1. Repeat-copy from template ALU sequence
2. Weighted random selection using LCG PRNG: `seed = (seed * 3877 + 29573) % 139968`

**Lambda implementation sketch**:
```lambda
let IM = 139968
let IA = 3877
let IC = 29573

pn main() {
    var seed = 42
    // repeat-copy fasta from ALU string
    // weighted random fasta from probability tables
    // output lines of 60 characters
}

pn random(seed: int, max: float) -> {seed: int, val: float} {
    var new_seed = (seed * IA + IC) % IM
    return {seed: new_seed, val: max * float(new_seed) / float(IM)}
}
```

Note: Since `pn` allows mutable state, `seed` can be a `var` modified in place rather than threaded through return values.

**Parameters**: N=1000 (validation), N=25,000,000 (performance)

### 5.6 k-nucleotide

**Algorithm**: Read a FASTA file, extract the sequence for ">THREE", count frequencies of all k-mers for k=1,2 and count specific 3,4,6,12,18-mer patterns.

**Lambda implementation approach**:
- Read file via `input(@path, 'text)`, extract ">THREE" section
- Use `map()` + `m.set(key, val)` for dynamic hash map:
  ```lambda
  pn count_kmers(seq: string, k: int) {
      var counts = map()
      var i = 0
      while (i <= len(seq) - k) {
          var kmer = slice(seq, i, i + k)
          // check if key exists, increment or set to 1
          // ...
          i = i + 1
      }
      return counts
  }
  ```
- Sort by frequency for 1-mer and 2-mer output
- Look up specific sequences for longer k-mers

**Note**: The `map()` + `m.set()` pattern works for dynamic key insertion (confirmed in `test/lambda/proc/vmap.ls`). Need to verify `m.get(key)` or bracket access for reading back values and checking key existence.

**Parameters**: Fasta input N=1000 (validation), N=25,000,000 (performance)

### 5.7 reverse-complement

**Algorithm**: Read FASTA sequences, output each with its reverse-complement using the standard nucleotide complement table.

**Lambda implementation approach**:
- Read file via `input(@path, 'text)`, split on `">"` headers
- For each sequence, strip header and newlines
- Apply complement mapping via chained `replace()` calls:
  ```lambda
  pn complement_seq(seq: string) -> string {
      // use upper() to normalize, then replace complement pairs
      // must use a two-pass approach for swapping pairs (A↔T, C↔G)
      // to avoid overwriting: first map to temp chars, then to final
      var s = upper(seq)
      s = replace(s, "A", "1")
      s = replace(s, "T", "A")
      s = replace(s, "1", "T")
      s = replace(s, "C", "2")
      s = replace(s, "G", "C")
      s = replace(s, "2", "G")
      // ... handle IUPAC codes M↔K, R↔Y, V↔B, H↔D, W↔W, S↔S, N↔N
      return s
  }
  ```
- Reverse the complemented string via `reverse()` (or reverse the chars array)
- Output in 60-character lines

**Parameters**: Input from fasta N=1000 (validation), N=25,000,000 (performance)

### 5.8 binary-trees

**Algorithm**: Recursively allocate full binary trees of increasing depth, walk and count nodes, then deallocate.

**Lambda implementation sketch**:
```lambda
pn make_tree(depth: int) {
    if (depth == 0) {
        return {left: null, right: null}
    }
    return {left: make_tree(depth - 1), right: make_tree(depth - 1)}
}

pn check(node) -> int {
    if (node.left == null) {
        return 1
    }
    return 1 + check(node.left) + check(node.right)
}

pn main() {
    let min_depth = 4
    let max_depth = max(min_depth + 2, n)
    let stretch_depth = max_depth + 1

    let stretch = make_tree(stretch_depth)
    print("stretch tree of depth " ++ string(stretch_depth)
          ++ "\t check: " ++ string(check(stretch)) ++ "\n")

    let long_lived = make_tree(max_depth)

    var depth = min_depth
    while (depth <= max_depth) {
        let iterations = shl(1, max_depth - depth + min_depth)
        var chk = 0
        var i = 0
        while (i < iterations) {
            chk = chk + check(make_tree(depth))
            i = i + 1
        }
        print(string(iterations) ++ "\t trees of depth " ++ string(depth)
              ++ "\t check: " ++ string(chk) ++ "\n")
        depth = depth + 2
    }

    print("long lived tree of depth " ++ string(max_depth)
          ++ "\t check: " ++ string(check(long_lived)) ++ "\n")
}
```

**Parameters**: N=10 (validation), N=21 (performance)
**Expected output** (N=10): matches `./benchmarksgame/public/download/binarytrees-output.txt`

### 5.9 pidigits

**Algorithm**: Compute digits of Pi using the unbounded spigot algorithm (Gibbons 2004). The algorithm maintains a linear fractional transformation (LFT) represented by big integers `(q, r, s, t)`:

1. **Compose**: multiply the LFT by the next term `k`
2. **Extract**: compute `floor((3*q + r) / (3*s + t))` and `floor((4*q + r) / (4*s + t))`
3. If both extracts agree, output the digit and reduce the LFT
4. If they disagree, compose with the next term and try again

**Lambda implementation approach using `decimal` with `N` suffix**:
```lambda
pn pidigits(n: int) {
    // arbitrary-precision integers via decimal N
    var q = 1N
    var r = 0N
    var s = 0N
    var t = 1N
    var k = 0N
    var i = 0
    var ns = 0

    while (i < n) {
        k = k + 1N
        var k2 = k * 2N + 1N

        // compose: (q,r,s,t) * (k, 4*k+2, 0, 2*k+1)
        var new_r = (2N * q + r) * k2
        var new_q = q * k
        var new_t = (2N * s + t) * k2
        var new_s = s * k
        q = new_q; r = new_r; s = new_s; t = new_t

        if (q > r) {
            // skip: numerator too small
        } else {
            // extract digit
            var d3 = floor((3N * q + r) / (3N * s + t))
            var d4 = floor((4N * q + r) / (4N * s + t))
            if (d3 == d4) {
                var d = int(d3)
                ns = ns * 10 + d
                i = i + 1

                if (i % 10 == 0 or i >= n) {
                    // print 10 digits per line, tab, colon, running count
                    // pad with spaces if last line < 10 digits
                    // ...
                }

                // reduce: subtract digit, multiply by 10
                r = (r - d3 * t) * 10N
                q = q * 10N
            }
        }
    }
}
```

**Key**: Lambda's `decimal` with `N` suffix provides unlimited-precision arithmetic backed by libmpdec. The operations `+`, `-`, `*`, `/`, `%`, `floor()` all work on `N` decimals. `floor(a / b)` gives integer division on decimals.

**Parameters**: N=30 (validation), N=10,000 (performance)
**Expected output** (N=30): matches `./benchmarksgame/public/download/pidigits-output.txt`

---

## 6. Comparison Languages

### 6.1 Comparison Targets

We compare Lambda against two reference languages — one JIT-compiled and one ahead-of-time compiled — using locally measured timings:

| Language | Implementation | Category | BENG Identifier |
|----------|---------------|----------|------------------|
| **Node.js** | V8 JIT | JIT compiled | `node` |
| **Go** | gc compiler | AOT compiled | `go` |

**Rationale**: Node.js is the closest peer (dynamically-typed, JIT-compiled scripting language). Go represents a statically-typed compiled language with GC — a realistic upper-bound target for Lambda's performance.

### 6.2 Expected Lambda Positioning

Based on our existing benchmark results (AWFY: ~53× slower than Node.js):

| Category | Expected vs Node.js | Expected vs Go | Rationale |
|----------|-------------------|----------------|-----------|
| Pure numeric (fannkuch, spectral-norm) | 10–50× slower | 10–50× slower | Lambda C2MIR JIT generates functional but unoptimized machine code |
| Float-heavy (n-body, mandelbrot) | 10–40× slower | 10–50× slower | Float unboxing not yet optimized |
| Memory/GC (binary-trees) | 1–8× slower | Comparable | Lambda's GC is relatively efficient for tree structures |
| String/I/O (fasta, reverse-complement) | 2–10× slower | 2–10× slower | String operations via arena allocation are efficient |
| Hash table (k-nucleotide) | 5–20× slower | 5–30× slower | `map()` hash table overhead vs optimized Go/V8 hash maps |
| Arb. precision (pidigits) | 1–5× slower | 1–5× slower | Both Lambda (libmpdec) and Node.js (GMP) use native C bignum libs |

---

## 7. Directory Structure & Output

### 7.1 Lambda Source Code

```
test/benchmark/beng/
├── fannkuch.ls             # fannkuch-redux benchmark
├── fannkuch.txt            # expected output for fannkuch (N=7)
├── nbody.ls                # n-body simulation
├── nbody.txt               # expected output for nbody (N=1000)
├── spectralnorm.ls         # spectral-norm eigenvalue computation
├── spectralnorm.txt        # expected output (N=100)
├── mandelbrot.ls           # mandelbrot set computation
├── mandelbrot.txt          # expected output (N=200)
├── fasta.ls                # fasta DNA sequence generator
├── fasta.txt               # expected output (N=1000)
├── knucleotide.ls          # k-nucleotide hash table benchmark
├── knucleotide.txt         # expected output (fasta N=1000 input)
├── revcomp.ls              # reverse-complement benchmark
├── revcomp.txt             # expected output (fasta N=1000 input)
├── binarytrees.ls          # binary-trees allocation benchmark
├── binarytrees.txt         # expected output (N=10)
├── pidigits.ls             # pi digits arbitrary-precision benchmark
├── pidigits.txt            # expected output (N=30)
├── run_bench.py            # benchmark runner script
└── input/                  # test input files
    ├── fasta_1000.txt      # pre-generated fasta output for N=1000
    └── fasta_25m.txt       # large input for performance runs
```

### 7.2 Result File

```
test/benchmark/Result_BENG.md
```

The result file will contain:
1. Benchmark environment (platform, versions, date)
2. Correctness verification (PASS/FAIL for each benchmark vs expected output)
3. Performance timing table: Lambda vs Node.js and Go
4. Geometric mean comparison
5. Per-benchmark analysis

### 7.3 Result File Format (Template)

```markdown
# BENG Benchmark Results — Lambda Script

**Date:** YYYY-MM-DD
**Lambda Script:** C2MIR JIT (v1.x)
**Node.js:** vXX.X.X (V8 JIT)
**Go:** vX.XX.X
**Platform:** macOS, Apple Silicon (Mac Mini)
**Runs per benchmark:** 5 (median reported)

## Correctness Verification

| # | Benchmark | Status | Output Match |
|---|-----------|--------|--------------|
| 1 | fannkuch-redux (N=7) | PASS/FAIL | ✓/✗ |
| 2 | n-body (N=1000) | PASS/FAIL | ✓/✗ |
| 3 | spectral-norm (N=100) | PASS/FAIL | ✓/✗ |
| 4 | mandelbrot (N=200) | PASS/FAIL | ✓/✗ |
| 5 | fasta (N=1000) | PASS/FAIL | ✓/✗ |
| 6 | k-nucleotide (fasta 1K) | PASS/FAIL | ✓/✗ |
| 7 | reverse-complement (fasta 1K) | PASS/FAIL | ✓/✗ |
| 8 | binary-trees (N=10) | PASS/FAIL | ✓/✗ |
| 9 | pidigits (N=30) | PASS/FAIL | ✓/✗ |

## Performance: Lambda vs Reference Languages

| Benchmark | Lambda | Node.js | Go | Lambda/Node | Lambda/Go |
|-----------|--------|---------|-----|-----------|----------|
| fannkuch-redux | Xs | Xs | Xs | Xx | Xx |
| n-body | Xs | Xs | Xs | Xx | Xx |
| spectral-norm | Xs | Xs | Xs | Xx | Xx |
| mandelbrot | Xs | Xs | Xs | Xx | Xx |
| fasta | Xs | Xs | Xs | Xx | Xx |
| k-nucleotide | Xs | Xs | Xs | Xx | Xx |
| reverse-comp | Xs | Xs | Xs | Xx | Xx |
| binary-trees | Xs | Xs | Xs | Xx | Xx |
| pidigits | Xs | Xs | Xs | Xx | Xx |
| **Geo Mean** | | | | **Xx** | **Xx** |

> All timings measured locally on the same machine
```

---

## 8. Implementation Plan

### Phase 1: Pure Numeric Benchmarks (Easy)

Implement benchmarks with no I/O dependencies — pure computation.

| Step | Benchmark | Key Lambda Features | Estimated Time |
|------|-----------|---------------------|----------------|
| 1.1 | **fannkuch-redux** | `pn`, `fill(n, 0)`, mutable arrays, while loops | 1 day |
| 1.2 | **n-body** | Adapt from AWFY `nbody.ls` to BENG format | 0.5 day |
| 1.3 | **spectral-norm** | `pn`, `fill(n, 0.0)`, nested while loops | 0.5 day |
| 1.4 | **mandelbrot** | `pn`, `shl`/`bor`/`band` bitwise packing, float math | 1 day |
| 1.5 | **binary-trees** | `pn`, recursive maps `{left: ..., right: ...}` | 1 day |

**Phase 1 total**: ~4 days

### Phase 2: String/I/O + Collection Benchmarks (Medium)

| Step | Benchmark | Key Lambda Features | Estimated Time |
|------|-----------|---------------------|----------------|
| 2.1 | **fasta** | `pn`, LCG PRNG, `%` modulo, `slice()`, string output | 1.5 days |
| 2.2 | **reverse-complement** | `input(@path, 'text)`, `replace()`, `reverse()`, `split()` | 1.5 days |
| 2.3 | **k-nucleotide** | `input()`, `map()` + `m.set(key, val)`, `slice()`, `sort()` | 2 days |

**Phase 2 total**: ~5 days

### Phase 3: Arbitrary-Precision Benchmark

| Step | Benchmark | Key Lambda Features | Estimated Time |
|------|-----------|---------------------|----------------|
| 3.1 | **pidigits** | `decimal` `N` suffix, `floor()`, unlimited precision arithmetic | 2 days |

**Phase 3 total**: ~2 days

### Phase 4: Benchmark Runner & Validation

| Step | Task | Estimated Time |
|------|------|----------------|
| 4.1 | Write `run_bench.py` (execute Lambda, compare outputs, collect timings) | 1 day |
| 4.2 | Generate expected output `.txt` files | 0.5 day |
| 4.3 | Validate all 9 benchmarks produce correct output | 1 day |
| 4.4 | Run Node.js and Go implementations for local comparison | 0.5 day |

**Phase 4 total**: ~3 days

### Phase 5: Performance Measurement & Report

| Step | Task | Estimated Time |
|------|------|----------------|
| 5.1 | Run performance benchmarks (5 runs each, median) | 0.5 day |
| 5.2 | Compile `Result_BENG.md` with all data | 1 day |
| 5.3 | Add Makefile target `make test-benchmark-beng` | 0.5 day |

**Phase 5 total**: ~2 days

---

## 9. Benchmark Runner Design

### 9.1 Runner Script (`run_bench.py`)

```python
#!/usr/bin/env python3
"""BENG Benchmark Runner for Lambda Script

Usage:
    python3 test/benchmark/beng/run_bench.py [--validate] [--perf] [num_runs]

Modes:
    --validate  Run with small inputs, check output correctness
    --perf      Run with large inputs, measure timing
"""

BENCHMARKS = [
    # (name, validation_args, perf_args, uses_file_input)
    ("fannkuch",     ["7"],            ["12"],           False),
    ("nbody",        ["1000"],         ["50000000"],     False),
    ("spectralnorm", ["100"],          ["5500"],         False),
    ("mandelbrot",   ["200"],          ["16000"],        False),
    ("fasta",        ["1000"],         ["25000000"],     False),
    ("knucleotide",  ["input/fasta_1000.txt"], ["input/fasta_25m.txt"], True),
    ("revcomp",      ["input/fasta_1000.txt"], ["input/fasta_25m.txt"], True),
    ("binarytrees",  ["10"],           ["21"],           False),
    ("pidigits",     ["30"],           ["10000"],        False),
]
```

### 9.2 Execution

```bash
# Validate correctness (small inputs)
python3 test/benchmark/beng/run_bench.py --validate

# Run performance benchmarks (5 runs, median)
python3 test/benchmark/beng/run_bench.py --perf 5

# Run a single benchmark
./lambda.exe run test/benchmark/beng/fannkuch.ls 12
./lambda.exe run test/benchmark/beng/nbody.ls 50000000
./lambda.exe run test/benchmark/beng/spectralnorm.ls 5500
./lambda.exe run test/benchmark/beng/pidigits.ls 10000
./lambda.exe run test/benchmark/beng/knucleotide.ls input/fasta_25m.txt
```

### 9.3 Timing Methodology

Following the BENG measurement approach:
- **External wall-clock timing** using Python `time.perf_counter_ns()` or `hyperfine`
- Each benchmark run **5 times**, report **median** elapsed time
- Includes full process lifecycle: startup + parsing + JIT compilation + execution
- Node.js and Go reference programs run locally on the same machine

### 9.4 Makefile Integration

```makefile
test-benchmark-beng:
	python3 test/benchmark/beng/run_bench.py --validate
	python3 test/benchmark/beng/run_bench.py --perf 5
```

---

## 10. Timeline Summary

| Phase | Description | Duration | Cumulative |
|-------|-------------|----------|------------|
| **1** | Pure numeric benchmarks (5 benchmarks) | 4 days | 4 days |
| **2** | String/I/O + collection benchmarks (3 benchmarks) | 5 days | 9 days |
| **3** | Arbitrary-precision benchmark (pidigits) | 2 days | 11 days |
| **4** | Runner, validation, reference data | 3 days | 14 days |
| **5** | Performance measurement & report | 2 days | 16 days |

**Total estimated effort: ~16 days (~3 weeks)**

### Milestone Checkpoints

| Milestone | Criteria | Target |
|-----------|---------|--------|
| M1 | fannkuch-redux, n-body, spectral-norm, mandelbrot, binary-trees producing correct output | Day 4 |
| M2 | fasta, reverse-complement, k-nucleotide producing correct output | Day 9 |
| M3 | pidigits producing correct output | Day 11 |
| M4 | `run_bench.py` passing validation for all 9 benchmarks | Day 14 |
| M5 | `Result_BENG.md` published with performance comparison vs Node.js and Go | Day 16 |

---

## 11. Comparison with Data from BENG Archive

### 11.1 Expected Lambda Performance vs Node.js and Go

Based on our existing AWFY results (~53× slower than Node.js on geometric mean), rough estimates:

| Benchmark | Expected Lambda (s) | Expected Lambda/Node | Expected Lambda/Go |
|-----------|-------------------|---------------------|--------------------|
| fannkuch-redux | 30–100s | 5–15× | 5–15× |
| n-body | 50–200s | 10–40× | 10–40× |
| spectral-norm | 10–50s | 7–30× | 7–30× |
| mandelbrot | 20–100s | 7–30× | 5–25× |
| fasta | 5–20s | 2–10× | 5–20× |
| k-nucleotide | 10–60s | 5–20× | 5–30× |
| reverse-comp | 1–10s | 0.5–5× | 0.5–5× |
| binary-trees | 5–30s | 1–8× | Comparable |
| pidigits | 2–20s | 1–5× | 1–5× |

These are rough estimates — actual results may vary significantly based on JIT optimization quality for each workload pattern. Pidigits is expected to be closer to reference languages because the bottleneck is the libmpdec library, not Lambda's own code generation.

---

## 12. Deferred Benchmark: regex-redux

### 12.1 Why Deferred

The regex-redux benchmark requires:
1. **Counting regex matches** in a string (9 patterns with character classes like `[cgt]gggtaaa|tttaccc[acg]`)
2. **Regex-based replacement** (5 patterns including variable-length matches like `<[^>]*>`)

Lambda's current string capabilities:
- `replace(str, old, new)` — **literal only** (exact `memcmp` matching, no patterns)
- `split(str, sep)` — **literal only**
- String patterns (e.g., `string P = \d+ "-" \w+`) — **type-level validators only**, checked via `is` for full-string matching; not usable for substring search, counting, or replacement
- No `count_matches`, `find_all`, `regex_match`, or `regex_replace` functions

### 12.2 Unblock Path

Any of the following would enable regex-redux:

| Option | Description | Effort |
|--------|-------------|--------|
| **A. Pattern-based string functions** | Add `count(str, pattern)`, `find_all(str, pattern)`, `replace(str, pattern, repl)` that accept string patterns | Medium (extend existing pattern engine) |
| **B. Regex library binding** | Link RE2 or PCRE2; expose `regex_match`, `regex_replace` | Medium (FFI work) |
| **C. Expose `cmd()` approach** | Use `cmd("grep", "-c", pattern, file)` for counting | Minimal (already possible, but not idiomatic) |

**Recommendation**: Option A is most idiomatic — it extends Lambda's existing string pattern system to support substring operations. This would also benefit other real-world use cases beyond benchmarking.

### 12.3 BENG Context

The BENG itself marks regex-redux as "Contentious. Different libraries." since it primarily measures third-party regex library quality rather than language implementation quality. Deferring this benchmark does not significantly impact the suite's coverage.

---

## 13. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Lambda JIT too slow for large workloads (N=50M n-body) | Benchmark takes hours | Reduce workload size while maintaining timing ratio validity |
| String operations bottleneck fasta output | Poor fasta results | Profile string allocation; batched string building via arrays + `str_join` |
| GC pauses affect binary-trees timing variance | High variance, unreliable results | Report min/median/max; increase run count |
| `map()` hash table too slow for k-nucleotide | Poor k-nucleotide results | Profile hash table; benchmark may reveal optimization opportunities |
| `decimal` arithmetic slower than GMP for pidigits | Higher pidigits ratio than expected | libmpdec is well-optimized; unlikely to be a bottleneck |
| Stack overflow on binary-trees depth=21 | Crash | Test stack depth; increase stack size if needed |
| Mandelbrot PBM binary output not working | Can't match reference output format | Fall back to text PBM (P1) or adapted output format |

---

## 14. Success Criteria

1. **Correctness**: All 9 implemented benchmarks produce correct output matching BENG reference (where format is compatible) or validated algorithm output (where adapted).
2. **Completeness**: All 9 benchmark `.ls` files exist in `test/benchmark/beng/` with corresponding `.txt` expected output files.
3. **Runner**: `run_bench.py` successfully validates all 9 benchmarks and collects timing data.
4. **Report**: `test/benchmark/Result_BENG.md` published with Lambda timings and comparison ratios against Node.js and Go.
5. **Reproducibility**: Any developer can run `python3 test/benchmark/beng/run_bench.py --validate` and see all PASS.
6. **Integration**: Makefile target `make test-benchmark-beng` available.

---

## 15. Idiomatic Lambda Features Showcased

This benchmark suite demonstrates several distinctive Lambda features:

| Feature | Used In | Description |
|---------|---------|-------------|
| `pn` procedural functions | All 9 benchmarks | Mutable state with `var`, while loops, array/map mutation |
| `decimal` with `N` suffix | pidigits | Unlimited-precision arithmetic backed by libmpdec |
| `map()` + `m.set(key, val)` | k-nucleotide | Dynamic mutable hash tables with runtime key insertion |
| `fill(n, val)` | fannkuch, spectral-norm | Array creation with fill values |
| `shl`/`shr`/`band`/`bor` | mandelbrot, binary-trees | Bitwise operations as system functions |
| `replace()` / `str[i]` / `reverse()` | reverse-complement | String manipulation functions (literal matching) |
| `split()` / `str_join()` | fasta, reverse-complement | String splitting and joining |
| `input(@path)` | k-nucleotide, reverse-complement | File-based input parsing |
| `floor()` on decimals | pidigits | Integer division on arbitrary-precision numbers |
| `sort(arr, comparator)` | k-nucleotide | Custom sort for frequency ranking |

---

## 16. Relationship to Other Benchmark Suites

| Suite | Focus | Benchmarks | Status | Results Location |
|-------|-------|------------|--------|-----------------|
| **AWFY** | OOP patterns | 14 | Complete | `test/benchmark/awfy/Result.md` |
| **R7RS** | Functional recursion | 10 | Complete | `test/benchmark/r7rs/Result.md` |
| **BENG** | Numeric, string, GC, arb-precision | 9 (+1 deferred) | **Proposed** | `test/benchmark/Result_BENG.md` |

Together, these three suites provide comprehensive coverage of Lambda Script's performance characteristics:

```
                  ┌──────────────────────────────────────────┐
                  │         Lambda Benchmark Coverage         │
                  ├──────────────┬──────────────┬────────────┤
                  │     AWFY     │     R7RS     │    BENG    │
                  ├──────────────┼──────────────┼────────────┤
                  │ OOP/objects  │ Recursion    │ Numeric    │
                  │ Polymorphism │ Closures     │ Array ops  │
                  │ Closures     │ Float math   │ Float math │
                  │ State mgmt   │ Tail calls   │ GC stress  │
                  │ Collections  │ Backtracking │ String I/O │
                  │ Graphs/trees │              │ Hash table │
                  │              │              │ Arb. prec. │
                  │              │              │ PRNG       │
                  └──────────────┴──────────────┴────────────┘
```

The BENG suite specifically fills the gap of testing Lambda against a widely recognized, publicly indexed benchmark that covers the broadest range of language implementations. With 9 of 10 benchmarks implemented, it provides near-complete coverage of the BENG task set.
