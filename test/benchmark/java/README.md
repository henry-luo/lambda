# Java benchmark ports

Handwritten native JVM ports of all 77 registered entries / 71 canonical
workloads. Sources are in `native/`; upstream AWFY Java sources compile unchanged.
See [the shared guide](../native_ports/README.md) for workload and timing details.

```sh
python3 test/benchmark/run_java_benchmarks.py --all --warmup 1 --verify-node --timeout 180 --output temp/java-validation.json
python3 test/benchmark/run_java_benchmarks.py --suite r7rs --bench fib
```

JDK 17+; checked with Homebrew OpenJDK 26.0.1. Compilation precedes measurement.
Source notices: [LICENSE.md](../native_ports/LICENSE.md).
