# Java benchmark ports

Native JVM ports of all 77 registered entries / 71 canonical workloads.
See [the shared guide](../native_ports/README.md) for implementation, timing,
provenance, workload parity, and regeneration details.

```sh
python3 test/benchmark/run_java_benchmarks.py --all --warmup 0 --verify-node --timeout 7200 --output temp/java-validation.json
python3 test/benchmark/run_java_benchmarks.py --suite r7rs --bench fib
```

Sources compile before measurement. Tested toolchain: Homebrew OpenJDK 26.0.1.
Source notices: [LICENSE.md](../native_ports/LICENSE.md).
