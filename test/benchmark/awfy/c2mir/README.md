# AWFY native C2MIR ports

These are direct C ports of the typed (`*2.ls`) AWFY workloads that fit the
C2MIR C subset.  They can be compiled and JIT-run directly:

```sh
mac-deps/mir/c2m test/benchmark/awfy/c2mir/nbody.c -eg
python3 test/benchmark/run_c2mir_benchmarks.py --suite awfy
```

The remaining AWFY macro benchmarks depend on substantially richer object and
JSON runtimes, so they are intentionally not represented by incomplete C
ports.
