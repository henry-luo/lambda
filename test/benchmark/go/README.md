# Native Go benchmarks

Each canonical workload is a separate Go program under `cmd/`.  For example,
the R7RS Fibonacci benchmark is `cmd/r7rs/fib`, and the AWFY collision detector
is `cmd/awfy/cd`.  The commands call shared implementation code in
`internal/bench`, but every benchmark is built and run as its own executable;
there is no all-benchmarks process or generic benchmark CLI.

Run one benchmark from the repository root:

```sh
go -C test/benchmark/go run ./cmd/awfy/havlak
```

For performance measurements, build first and run the resulting executable so
compilation is outside the timed region:

```sh
go -C test/benchmark/go build -o temp/go-benchmarks/awfy_havlak ./cmd/awfy/havlak
./temp/go-benchmarks/awfy_havlak
```

Run the complete isolated suite (each process is timed only after its binary is
built):

```sh
python3 test/benchmark/run_go_benchmarks.py
python3 test/benchmark/run_go_benchmarks.py --suite jetstream
```

The runner writes disposable binaries only under `temp/go-benchmarks` and
uses the repository root as each program's working directory, which keeps
fixture-based workloads reproducible.
