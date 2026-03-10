// Object type definition and basic usage
type Point { x: int, y: int }
let p = <Point x: 3, y: 4>
p.x
p.y

// Methods: zero-arg and one-arg
type Counter {
    value: int;
    fn double() => value * 2
    fn add(n: int) => value + n
}
let c = <Counter value: 5>
c.double()
c.add(3)

// Type checking
p is Point
p is object
5 is Point
