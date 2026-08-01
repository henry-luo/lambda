# R7RS native C2MIR ports

Each source is a direct C port of the typed (`*2.ls`) R7RS workload.  They use
only declarations that C2MIR can resolve from the system C library and can be
run without the retired Lambda C2MIR backend:

```sh
mac-deps/mir/c2m test/benchmark/r7rs/c2mir/fib.c -eg
python3 test/benchmark/run_c2mir_benchmarks.py --suite r7rs
```

The output is deliberately a stable `name: PASS` line; timing belongs to the
runner because C2MIR does not consistently support platform timing headers.
