# Erlang benchmark ports

Handwritten native BEAM ports of all 77 registered entries / 71 canonical
workloads. Sources are in `native/`. See [the shared guide](../native_ports/README.md)
for workload and timing details.

```sh
python3 test/benchmark/run_erlang_benchmarks.py --all --warmup 1 --verify-node --timeout 180 --output temp/erlang-validation.json
python3 test/benchmark/run_erlang_benchmarks.py --suite r7rs --bench fib
```

OTP 27+; checked with Homebrew Erlang/OTP 29.1.1. Compilation precedes measurement.
Source notices: [LICENSE.md](../native_ports/LICENSE.md).
