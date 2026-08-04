# C2MIR Double vs. Int Benchmark Results

This comparison measures native C2MIR C benchmark ports using integer state
versus matched counterparts whose computed numeric state uses `double`.

- Runs per source: 7
- Timing: median workload time from `__TIMING__`
- Scope: C2MIR-generated code only; C2MIR compilation and process startup are excluded
- Validation: all 24 double counterparts produced the same output as their integer counterparts
- Lower ratios are faster: `double / int`
- Command: `python3 test/benchmark/run_c2mir_double_comparison.py --runs 7`

## Results

| Suite | Benchmark | Int (ms) | Double (ms) | Double / Int |
|-------|-----------|---------:|------------:|-------------:|
| R7RS | ack | 11.516 | 11.006 | 0.96x |
| R7RS | cpstak | 0.242 | 0.258 | 1.07x |
| R7RS | fib | 1.003 | 1.021 | 1.02x |
| R7RS | nqueens | 0.116 | 0.157 | 1.35x |
| R7RS | sum | 0.258 | 0.691 | 2.68x |
| R7RS | tak | 0.126 | 0.140 | 1.11x |
| AWFY | bounce | 0.026 | 0.027 | 1.03x |
| AWFY | list | 0.020 | 0.020 | 1.00x |
| AWFY | permute | 0.022 | 0.030 | 1.37x |
| AWFY | queens | 0.018 | 0.022 | 1.22x |
| AWFY | sieve | 0.017 | 0.017 | 0.99x |
| Kostya | collatz | 199.841 | 1397.786 | 6.99x |
| Kostya | json_gen | 1.444 | 1.850 | 1.28x |
| Kostya | levenshtein | 0.851 | 0.781 | 0.92x |
| Kostya | primes | 1.520 | 2.366 | 1.56x |
| Larceny | array1 | 0.304 | 0.576 | 1.89x |
| Larceny | diviter | 254.401 | 618.039 | 2.43x |
| Larceny | divrec | 4.825 | 5.344 | 1.11x |
| Larceny | paraffins | 0.043 | 0.043 | 1.00x |
| Larceny | pnpoly | 1.897 | 1.688 | 0.89x |
| Larceny | primes | 1.420 | 2.266 | 1.60x |
| Larceny | puzzle | 1.268 | 1.572 | 1.24x |
| Larceny | quicksort | 0.201 | 0.239 | 1.19x |
| Larceny | triangl | 54.158 | 48.740 | 0.90x |

## Summary

- Geometric mean: **1.33x** (`double` is slower overall)
- Double was faster in **5 of 24** comparisons.
- Largest slowdown: `collatz`, at **6.99x**, because the double version replaces integer parity/modulo operations with floating-point operations.
- Other significant slowdowns: `sum` at **2.68x**, `diviter` at **2.43x**, `array1` at **1.89x**, and Larceny `primes` at **1.60x**.
- Slight double wins appeared in `pnpoly` (0.89x), `triangl` (0.90x), `levenshtein` (0.92x), and `ack` (0.96x); these small differences should be treated as workload/code-generation effects rather than a general double advantage.

## Porting notes

The counterparts use `double` for the benchmark's numeric payload, accumulators,
state arrays, and recursive numeric arguments wherever the algorithm permits it.
Integral values remain for C array indices, loop selectors, bitwise state,
allocation sizes, and `main`'s C ABI status result. These are control or ABI
requirements rather than the numeric workload being compared.

The `json_gen` counterpart also models the original signed 32-bit PRNG overflow
and remainder behavior using double arithmetic, so its generated output remains
identical to the integer version.
