# BENG Benchmark Results

**Benchmarks Game** — Lambda Script vs Node.js

- Runs per benchmark: 3
- Node.js version: v22.13.0
- Date: 2026-02-28 13:53:38
- Timing: wall-clock (startup + compile/JIT + execution)

## Results

| Benchmark | Category | N | Lambda | Node.js | Ratio |
|-----------|----------|---|--------|---------|-------|
| binarytrees | gc | 10 | 67.4 ms | 45.3 ms | 1.49x |
| fannkuch | combinatorial | 7 | 18.4 ms | 43.0 ms | 0.43x |
| fasta | string | 1000 | 48.8 ms | 45.3 ms | 1.08x |
| knucleotide | hashmap | file | 23.9 ms | 45.9 ms | 0.52x |
| mandelbrot | numeric | 200 | 54.2 ms | 46.3 ms | 1.17x |
| nbody | numeric | 1000 | 26.9 ms | 62.2 ms | 0.43x |
| pidigits | bigint | 30 | 16.5 ms | 45.3 ms | 0.36x |
| regexredux | regex | file | 19.6 ms | 40.8 ms | 0.48x |
| revcomp | string | file | 20.8 ms | 43.4 ms | 0.48x |
| spectralnorm | numeric | 100 | 43.2 ms | 56.8 ms | 0.76x |

**Geometric mean ratio: 0.64x** (Lambda wins: 7, Node.js wins: 3)

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
