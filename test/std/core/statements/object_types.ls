// Test: Object Types
// Layer: 2 | Category: statement | Covers: fields, methods, inheritance, defaults, constraints, is

// ===== Basic object type =====
type Person {
    name: string
    age: int
}
let alice = <Person name: "Alice", age: 30>
alice.name
alice.age
alice is Person

// ===== Object with defaults =====
type Server {
    host: string = "localhost"
    port: int = 8080
    tls: bool = false
}
let s1 = <Server>
s1.host
s1.port
s1.tls

let s2 = <Server host: "prod.com", tls: true>
s2.host
s2.port
s2.tls

// ===== Object methods =====
type Vec2 {
    x: float
    y: float
    fn length() => sqrt(~.x ** 2 + ~.y ** 2)
    fn scale(factor: float) => <Vec2 x: ~.x * factor, y: ~.y * factor>
}
let v = <Vec2 x: 3.0, y: 4.0>
v.length()
let v2 = v.scale(2.0)
v2.x
v2.y

// ===== Inheritance =====
type Vehicle { make: string, year: int }
type Car : Vehicle { doors: int = 4 }
type Truck : Vehicle { payload: float }
let car = <Car make: "Toyota", year: 2023>
car.make
car.year
car.doors
car is Car
car is Vehicle

let truck = <Truck make: "Ford", year: 2022, payload: 5000.0>
truck.make
truck.payload
truck is Truck
truck is Vehicle

// ===== Multi-level inheritance =====
type Shape { color: string = "black" }
type Circle : Shape { radius: float }
type ColoredCircle : Circle { opacity: float = 1.0 }
let cc = <ColoredCircle color: "red", radius: 5.0>
cc.color
cc.radius
cc.opacity
cc is ColoredCircle
cc is Circle
cc is Shape

// ===== Object update/copy =====
type Settings { theme: string = "light", font_size: int = 14 }
let base = <Settings>
let dark = {base, theme: "dark"}
base.theme
dark.theme
dark.font_size

// ===== Field constraints with that =====
type Age {
    value: int
    that value >= 0 and value <= 150
}
let valid_age = <Age value: 25>
valid_age.value

// ===== Object constraint =====
type DateRange {
    start: int
    end: int
    that start <= end
}
let range = <DateRange start: 1, end: 10>
range.start
range.end

// ===== Mutation methods =====
type Counter {
    count: int = 0
    pn increment() { ~.count = ~.count + 1 }
    pn decrement() { ~.count = ~.count - 1 }
    fn value() => ~.count
}
let ctr = <Counter>
ctr.value()
ctr.increment()
ctr.value()
ctr.increment()
ctr.increment()
ctr.value()

// ===== Method returning self type =====
type Builder {
    parts: list = ()
    fn add(part: string) => <Builder parts: (*~.parts, part)>
    fn build() => ~.parts | join(", ")
}
let b = <Builder>
    .add("A")
    .add("B")
    .add("C")
b.build()
