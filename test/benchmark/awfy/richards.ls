// S9.1.2/S9.1.3: the untyped handle-store kernel has one writable owner.
import ~~._richards_core

pn main() {
    let t0 = clock()
    let pass = run_richards_benchmark(50)
    let t1 = clock()
    if (pass) { print("Richards: PASS\n") }
    else { print("Richards: FAIL\n") }
    print("__TIMING__:" ++ ((t1 - t0) * 1000.0) ++ "\n")
}
