// Test direct struct access optimization in object methods
// Phase 5: fn method field loading via _self_data->field
// Phase 6: pn method field write-back via _self_data->field = val

// ---- Phase 5: fn method field loading ----

// Test 1: fn method reads float fields directly
type Vec2 { x: float, y: float; fn mag() => math.sqrt(x * x + y * y) }
let v = {Vec2 x: 3.0, y: 4.0}
v.mag()

// Test 2: fn method reads mixed types (string + int + bool)
type Entry {
    label: string,
    count: int,
    active: bool;
    fn summary() => label ++ ":" ++ string(count)
    fn is_active() => active
}
let e = {Entry label: "hits", count: 42, active: true}
e.summary()
e.is_active()

// Test 3: fn method with parameter combining self fields and args
type Adder {
    base: int;
    fn add(n: int) => base + n
    fn mul(n: int) => base * n
}
let a = {Adder base: 10}
a.add(5)
a.mul(3)

// Test 4: multiple fn methods on same object, calling in sequence
type Stats {
    min: int, max: int;
    fn range() => max - min
    fn mid() => (min + max) / 2
}
let s = {Stats min: 10, max: 50}
[s.range(), s.mid()]

// Test 5: fn method accessing bool field in condition
type Gate {
    open: bool;
    fn status() => if (open) "open" else "closed"
}
let g1 = {Gate open: true}
let g2 = {Gate open: false}
g1.status()
g2.status()

// ---- Phase 6: pn method field write-back ----

// Test 6: pn method write-back on int field
type Tally {
    n: int = 0;
    pn bump() { n = n + 1 }
    pn add(x: int) { n = n + x }
    fn value() => n
}
let t = {Tally}
t.bump()
t.bump()
t.bump()
t.value()

// Test 7: pn method write-back on float fields
type Pos {
    x: float = 0.0,
    y: float = 0.0;
    pn move(dx: float, dy: float) {
        x = x + dx
        y = y + dy
    }
    fn coords() => [x, y]
}
let pos = {Pos}
pos.move(1.5, 2.5)
pos.coords()
pos.move(-0.5, 3.5)
pos.coords()

// Test 8: pn write-back then fn read on same object
type Accum {
    total: int = 0,
    calls: int = 0;
    pn record(val: int) {
        total = total + val
        calls = calls + 1
    }
    fn avg() => total / calls
}
let ac = {Accum}
ac.record(10)
ac.record(20)
ac.record(30)
ac.avg()

// Test 9: pn method assigns literal value (not derived from field)
type Toggle {
    on: bool = false;
    pn enable() { on = true }
    pn disable() { on = false }
    fn state() => on
}
let tog = {Toggle}
tog.state()
tog.enable()
tog.state()
tog.disable()
tog.state()

// Test 10: multiple pn mutations interleaved with fn reads
type Wallet {
    balance: int = 0;
    pn deposit(amt: int) { balance = balance + amt }
    pn withdraw(amt: int) { balance = balance - amt }
    fn check() => balance
}
let w = {Wallet}
w.deposit(100)
w.check()
w.withdraw(30)
w.check()
w.deposit(50)
w.check()

// Test 11: pn write-back with string field
type Logger {
    last: string = "";
    pn log(msg: string) { last = msg }
    fn peek() => last
}
let lg = {Logger}
lg.log("hello")
lg.peek()
lg.log("world")
lg.peek()

// Test 12: fn and pn methods combined — compute then mutate
type Counter {
    val: int = 0;
    fn doubled() => val * 2
    pn inc() { val = val + 1 }
}
let cnt = {Counter}
cnt.doubled()
cnt.inc()
cnt.inc()
cnt.inc()
cnt.doubled()
