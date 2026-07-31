# Kostya native C2MIR ports

These C ports retain the original workload sizes and use only the C subset
accepted by MIR's `c2m` driver.  Run the supported ports with:

```sh
python3 test/benchmark/run_c2mir_benchmarks.py --suite kostya
```

`json_gen` is included using a bounded native serialization buffer. This retains
the source's generated JSON shape and work size without relying on C2MIR-hostile
dynamic string APIs.
