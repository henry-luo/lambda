// Procedural half of test/lambda/typed_param_direct_access.ls.
// A `pn` method may only be called from procedural context (S12.1), so the
// object-param case that mutates through `inc()` lives here; every purely
// functional case stays in the functional test and reports its value directly.

// Test 9: object param with fn method, mutated through a pn method
type Counter {
    val: int = 0,
    fn doubled() => val * 2
    pn inc() { val = val + 1 }
}
fn read_counter(c: Counter) => c.val

pn main() {
    var cnt = <Counter>
    cnt.inc()
    cnt.inc()
    cnt.inc()
    print(read_counter(cnt)); print("\n")
    print(cnt.doubled()); print("\n")
}
