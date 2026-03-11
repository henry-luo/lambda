// Object type: basic definition and creation
type Point { x: int, y: int }
let p = <Point x: 3, y: 4>
p
p.x
p.y

// Object with methods
type Counter {
    value: int;
    fn double() => value * 2
    fn add(n: int) => value + n
}
let c = <Counter value: 5>
c.value
c.double()
c.add(3)

// Type checking: nominal
[p is Point, p is object]
[5 is Point, "hi" is Point]

// Type checking: different types with same shape
type Size { w: int, h: int }
let s = <Size w: 10, h: 20>
[s is Size, s is object]

// Multiple method calls
type Calc {
    a: int, b: int;
    fn total() => a + b
    fn diff() => a - b
    fn product() => a * b
}
let calc = <Calc a: 10, b: 3>
[calc.total(), calc.diff(), calc.product()]
