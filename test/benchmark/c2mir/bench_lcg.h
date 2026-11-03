#ifndef C2MIR_BENCH_LCG_H
#define C2MIR_BENCH_LCG_H

/* Match the Lambda int53 generator: its largest intermediate is < 2^41.
 * A 32-bit multiply overflows before the modulus and changes the workload. */
static int bench_lcg_next(int seed) {
    return (int)(((long long)seed * 1664525 + 1013904223) % 1000000);
}

#endif
