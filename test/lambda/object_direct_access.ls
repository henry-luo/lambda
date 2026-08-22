// Test direct struct access optimization in object methods
// Phase 5: fn method field loading via _self_data->field
// Phase 6: pn method field write-back via _self_data->field = val

// ---- Phase 5: fn method field loading ----

// Test 1: fn method reads float fields directly
type Vec2 { x: float, y: float, fn mag() => math.sqrt(x * x + y * y) }
let v = <Vec2 x: 3.0, y: 4.0>
v.mag()

// Test 2: fn method reads mixed types (string + int + bool)
type Entry {
    label: string,
    count: int,
    active: bool,
    fn summary() => label ++ ":" ++ (count)
    fn is_active() => active
}
let e = <Entry label: "hits", count: 42, active: true>
e.summary()
e.is_active()

// Test 3: fn method with parameter combining self fields and args
type Adder {
    base: int,
    fn add(n: int) => base + n
    fn mul(n: int) => base * n
}
let a = <Adder base: 10>
a.add(5)
a.mul(3)

// Test 4: multiple fn methods on same object, calling in sequence
type Stats {
    min: int, max: int,
    fn range() => max - min
    fn mid() => (min + max) / 2
}
let s = <Stats min: 10, max: 50>;
[s.range(), s.mid()]

// Test 5: fn method accessing bool field in condition
type Gate {
    open: bool,
    fn status() => if (open) "open" else "closed"
}
let g1 = <Gate open: true>
let g2 = <Gate open: false>
g1.status()
g2.status()
