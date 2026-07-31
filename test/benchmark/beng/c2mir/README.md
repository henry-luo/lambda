# BENG native C2MIR ports

The portable numeric, allocation, and permutation workloads are available as
native C2MIR programs:

```sh
python3 test/benchmark/run_c2mir_benchmarks.py --suite beng
```

The remaining BENG text-processing and bignum workloads depend on regex,
streaming I/O, or arbitrary-precision arithmetic not supplied by C2MIR.
