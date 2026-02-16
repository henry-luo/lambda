// AWFY Benchmark Harness for Lambda Script
// Usage: ./lambda.exe run test/benchmark/awfy/harness.ls <benchmark> [num_iterations] [inner_iterations]
//
// Each benchmark is a standalone .ls file in this directory.
// This harness is a reference for how benchmarks should be structured.
//
// Each benchmark file must define:
//   pn benchmark()  - runs the benchmark, returns the result
//   pn main()       - runs the benchmark and prints PASS/FAIL
//
// For performance measurement, use external timing:
//   time ./lambda.exe run test/benchmark/awfy/sieve.ls
//   hyperfine './lambda.exe run test/benchmark/awfy/sieve.ls'
//
// Available benchmarks (micro):
//   sieve      - Sieve of Eratosthenes (expected: 669)
//   permute    - Permutation generation (expected: 8660)
//   queens     - N-Queens solver (expected: true)
//   towers     - Towers of Hanoi (expected: 8191)
//   bounce     - Ball bouncing simulation (expected: 1331)
//   list       - Recursive linked-list Tak (expected: 10)
//   storage    - Recursive tree allocation (expected: 5461)
//   mandelbrot - Mandelbrot set computation (expected: 191)
//   nbody      - N-Body gravitational simulation (expected: -0.1690859889909308)

pn main() {
    print("AWFY Benchmark Harness for Lambda Script\n")
    print("=========================================\n")
    print("\n")
    print("Run individual benchmarks:\n")
    print("  ./lambda.exe run test/benchmark/awfy/sieve.ls\n")
    print("  ./lambda.exe run test/benchmark/awfy/permute.ls\n")
    print("  ./lambda.exe run test/benchmark/awfy/queens.ls\n")
    print("  ./lambda.exe run test/benchmark/awfy/towers.ls\n")
    print("  ./lambda.exe run test/benchmark/awfy/bounce.ls\n")
    print("  ./lambda.exe run test/benchmark/awfy/list.ls\n")
    print("  ./lambda.exe run test/benchmark/awfy/storage.ls\n")
    print("  ./lambda.exe run test/benchmark/awfy/mandelbrot.ls\n")
    print("  ./lambda.exe run test/benchmark/awfy/nbody.ls\n")
    print("\n")
    print("For performance measurement:\n")
    print("  time ./lambda.exe run test/benchmark/awfy/<benchmark>.ls\n")
    print("  hyperfine './lambda.exe run test/benchmark/awfy/<benchmark>.ls'\n")
}
