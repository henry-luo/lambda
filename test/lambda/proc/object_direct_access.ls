// Procedural half of test/lambda/object_direct_access.ls — Phase 6.
// A `pn` method may only be called from procedural context (S12.1) and
// mutating methods need a `var` receiver (S9), so every pn write-back case
// lives here. The Phase 5 fn-method cases stay in the functional test and
// report their values directly.

type Tally {
    n: int = 0,
    pn bump() { n = n + 1 }
    pn add(x: int) { n = n + x }
    fn value() => n
}
type Pos {
    x: float = 0.0,
    y: float = 0.0,
    pn move(dx: float, dy: float) {
        x = x + dx
        y = y + dy
    }
    fn coords() => [x, y]
}
type Accum {
    total: int = 0,
    calls: int = 0,
    pn record(val: int) {
        total = total + val
        calls = calls + 1
    }
    fn avg() => total / calls
}
type Toggle {
    on: bool = false,
    pn enable() { on = true }
    pn disable() { on = false }
    fn state() => on
}
type Wallet {
    balance: int = 0,
    pn deposit(amt: int) { balance = balance + amt }
    pn withdraw(amt: int) { balance = balance - amt }
    fn check() => balance
}
type Logger {
    last_msg: string = "",
    pn log(msg: string) { last_msg = msg }
    fn peek() => last_msg
}
type Counter {
    val: int = 0,
    fn doubled() => val * 2
    pn inc() { val = val + 1 }
}

pn main() {
    // ---- Phase 6: pn method field write-back ----

    // Test 6: pn method write-back on int field
    var t = <Tally>
    t.bump()
    t.bump()
    t.bump()
    print(t.value()); print("\n")

    // Test 7: pn method write-back on float fields
    var pos = <Pos>
    pos.move(1.5, 2.5)
    print(pos.coords()); print("\n")
    pos.move(-0.5, 3.5)
    print(pos.coords()); print("\n")

    // Test 8: pn write-back then fn read on same object
    var ac = <Accum>
    ac.record(10)
    ac.record(20)
    ac.record(30)
    print(ac.avg()); print("\n")

    // Test 9: pn method assigns literal value (not derived from field)
    var tog = <Toggle>
    print(tog.state()); print("\n")
    tog.enable()
    print(tog.state()); print("\n")
    tog.disable()
    print(tog.state()); print("\n")

    // Test 10: multiple pn mutations interleaved with fn reads
    var w = <Wallet>
    w.deposit(100)
    print(w.check()); print("\n")
    w.withdraw(30)
    print(w.check()); print("\n")
    w.deposit(50)
    print(w.check()); print("\n")

    // Test 11: pn write-back with string field
    var lg = <Logger>
    lg.log("hello")
    print(lg.peek()); print("\n")
    lg.log("world")
    print(lg.peek()); print("\n")

    // Test 12: fn and pn methods combined — compute then mutate
    var cnt = <Counter>
    print(cnt.doubled()); print("\n")
    cnt.inc()
    cnt.inc()
    cnt.inc()
    print(cnt.doubled()); print("\n")

}
