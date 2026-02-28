# BENG Benchmark Results

**Benchmarks Game** — Lambda Script vs Node.js

- Runs per benchmark: 3
- Node.js version: v22.13.0
- Date: 2026-02-28 13:39:48
- Timing: wall-clock (startup + compile/JIT + execution)

## Results

| Benchmark | Category | N | Lambda | Node.js | Ratio |
|-----------|----------|---|--------|---------|-------|
| binarytrees | gc | 10 | 70.3 ms | 48.7 ms | 1.44x |
| fannkuch | combinatorial | 7 | 22.1 ms | 48.0 ms | 0.46x |
| fasta | string | 1000 | 22.1 ms | 48.8 ms | 0.45x |
| knucleotide | hashmap | file | 26.6 ms | 52.7 ms | 0.51x |
| mandelbrot | numeric | 200 | 61.9 ms | 52.2 ms | 1.19x |
| nbody | numeric | 1000 | 27.2 ms | 46.2 ms | 0.59x |
| pidigits | bigint | 30 | 20.3 ms | 43.5 ms | 0.47x |
| revcomp | string | file | 23.6 ms | 48.9 ms | 0.48x |
| spectralnorm | numeric | 100 | 58.1 ms | 49.4 ms | 1.18x |

**Geometric mean ratio: 0.67x** (Lambda wins: 6, Node.js wins: 3)

## Notes

- Ratio < 1.0 means Lambda is faster; > 1.0 means Node.js is faster
- Lambda uses C2MIR JIT compilation (release build with LTO)
- Node.js uses V8 JIT compilation
- N column is the workload size parameter for each benchmark. N values are kept small for baseline testing; increase for production benchmarks
- File-based benchmarks (knucleotide, revcomp) use `input/fasta_1000.txt`
- To re-run: `python3 test/benchmark/beng/run_bench.py [num_runs]`

## Lambda Issues Discovered

The following language/runtime issues were encountered while implementing the BENG benchmarks.

### Parser / Language Limitations

1. **`fn` closures don't work inside `pn` functions** — Using `sort(entries, fn(a, b) { ... })` inside a `pn` function causes an "Unexpected syntax" parser error. Workaround: manual bubble sort in `knucleotide.ls`.

2. **`sort()` lacks custom comparator support** — `sort()` only accepts an optional `"desc"` string, not a comparison function. Any non-trivial sorting (e.g. sort by frequency descending, then alphabetically) requires a manual sort implementation.

3. **`@"..."` path literal syntax doesn't work** — Scripts originally used `@"path/to/file"` but this is not valid. Workaround: use plain `"..."` strings for file paths.

4. **`'text` (unclosed symbol) doesn't parse** — Symbol literals require a closing quote: `'text'`, not `'text`.

### Runtime / Code Generation Issues

5. **Deeply nested control flow causes C2MIR compilation failure** — `fannkuch.ls`'s original structure with `while(found==0 and running==1)` nested inside another `while` generated invalid C code at ~line 1048 of the transpiler output. Workaround: flatten to a single loop with early `r = 1` initialization.

6. **Array reads return `Item` type, not `int`** — Using `perm[0]` directly as an array index produces bogus values (e.g. 1222974912 instead of 3). Must explicitly call `int(perm[0])` to convert. This is a significant footgun for procedural code.

7. **`reverse()` on a char array returns a single joined string** — `reverse(["a","b","c"])` returns `"cba"` (a single string), not `["c","b","a"]` (a reversed array). This forced a complete rewrite of `revcomp.ls` from a char-array approach to a string-based approach.

### Impact Assessment

Issues **#1**, **#2**, and **#6** are the most impactful for general Lambda usability — they forced significant algorithm rewrites that would not be needed in most languages. Issue **#5** limits the complexity of procedural code that can be JIT-compiled.
