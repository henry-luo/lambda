# Larceny native C2MIR ports

The supported ports preserve the source workload dimensions and algorithms for
array processing, recursive/iterative division, point-in-polygon, sieve,
quicksort, ray tracing, triangle-solitaire search, N-Queens, and paraffin
isomer counting:

```sh
python3 test/benchmark/run_c2mir_benchmarks.py --suite larceny
```

The remaining symbolic and list benchmarks remain out of scope until a native
representation can preserve their original allocation and ownership semantics.
