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
| 1 | fannkuch-redux (N=7) | | |
| 2 | n-body (N=1000) | | |
| 3 | spectral-norm (N=100) | | |
| 4 | mandelbrot (N=200) | | |
| 5 | fasta (N=1000) | | |
| 6 | k-nucleotide (fasta 1K) | | |
| 7 | reverse-complement (fasta 1K) | | |
| 8 | binary-trees (N=10) | | |
| 9 | pidigits (N=30) | | |

## Performance: Lambda vs Reference Languages

| Benchmark | Lambda | Node.js | Go | Lambda/Node | Lambda/Go |
|-----------|--------|---------|-----|-----------|----------|
| fannkuch-redux | | | | | |
| n-body | | | | | |
| spectral-norm | | | | | |
| mandelbrot | | | | | |
| fasta | | | | | |
| k-nucleotide | | | | | |
| reverse-comp | | | | | |
| binary-trees | | | | | |
| pidigits | | | | | |
| **Geo Mean** | | | | **** | **** |

> All timings measured locally on the same machine

## Notes

- regex-redux deferred (requires pattern-based string search/count/replace)
- k-nucleotide and reverse-complement use file input instead of stdin
- Mandelbrot output format: TBD (P4 binary or P1 text PBM)
