// JetStream Richards typed wrapper; the shared handle-store kernel is used by both suites.
import test.benchmark.richards2_core

pn main() {
    let t0 = clock()
    let pass = run_richards_benchmark(50)
    let t1 = clock()
    if (pass) {
        print("richards: PASS\n")
    } else {
        print("richards: FAIL\n")
    }
    print("__TIMING__:" ++ ((t1 - t0) * 1000.0) ++ "\n")
}
