# Native C2MIR benchmark coverage

`mac-deps/mir/c2m` compiles and JIT-runs the native C ports. Run all supported
benchmarks with:

```sh
python3 test/benchmark/run_c2mir_benchmarks.py
```

## Covered workloads

| Suite | Coverage |
| --- | --- |
| R7RS | 10/10 canonical workloads |
| AWFY | 9 of 14 workloads: all micro/compute ports that do not depend on the SOM object runtime |
| BENG | 8 numeric/allocation/permutation/FASTA/text workloads |
| Kostya | 7/7 canonical workloads |
| Larceny | 11/12 canonical workloads |
| JetStream | hash-map; several other JetStream rows duplicate an existing workload under another suite |

The two Lambda source variants (`name.ls` and `name2.ls`) represent the same
benchmark with different Lambda typing; one native C port covers that workload.

## Deliberate exclusions

- `cow_document_edit`: exercises Lambda's copy-on-write document runtime, not a
  portable C workload.
- AWFY `cd`, `deltablue`, `havlak`, `json`, and `richards`: depend on the
  complete SOM-style object/constraint/parser runtime. A simplified C version
  would measure a different benchmark.
- BENG `pidigits` requires arbitrary-precision arithmetic.
- Larceny `deriv`: represents symbolic Lambda data structures rather than a
  numerical algorithm with a direct native representation.
- JetStream cryptography, regex, splay, Navier–Stokes, ray tracing, and cube
  ports need sizeable dedicated C implementations or unsupported input/runtime
  facilities. `base64`, `nbody`, `deltablue`, and `richards` already have
  equivalent covered workloads in other suites.
