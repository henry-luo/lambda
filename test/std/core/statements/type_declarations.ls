// Test: Type Declarations
// Layer: 2 | Category: statement | Covers: type alias, union, object types, element types

// ===== Type alias =====
type Age = int
let a: Age = 25
a
type(a)

// ===== Union type alias =====
type StringOrInt = string | int
fn show_val(v: StringOrInt) => match v {
    case string: "string:" & v
    case int: "int:" & str(v)
}
show_val("hello")
show_val(42)

// ===== Object type with fields =====
type Point { x: float, y: float }
let p = {Point x: 1.0, y: 2.0}
p.x
p.y
p is Point

// ===== Object type with defaults =====
type Config {
    host: string = "localhost"
    port: int = 8080
    debug: bool = false
}
let cfg = {Config}
cfg.host
cfg.port
cfg.debug

let cfg2 = {Config host: "example.com", debug: true}
cfg2.host
cfg2.port
cfg2.debug

// ===== Object type with methods =====
type Rectangle {
    width: float
    height: float
    fn area() => ~.width * ~.height
    fn perimeter() => 2 * (~.width + ~.height)
}
let rect = {Rectangle width: 3.0, height: 4.0}
rect.area()
rect.perimeter()

// ===== Type inheritance =====
type Animal { name: string, sound: string = "..." }
type Dog : Animal { sound: string = "woof" }
type Cat : Animal { sound: string = "meow" }
let d = {Dog name: "Rex"}
let c = {Cat name: "Whiskers"}
d.name
d.sound
c.name
c.sound
d is Dog
d is Animal
c is Cat
c is Animal

// ===== Type with constraints =====
type Positive {
    value: int
    that value > 0
}
let pos = {Positive value: 5}
pos.value

// ===== Nominal typing (different types not equal) =====
type Meters { value: float }
type Seconds { value: float }
let m = {Meters value: 10.0}
let s = {Seconds value: 10.0}
m is Meters
m is Seconds

// ===== Element type =====
type TodoItem = <todo done: bool> string
let task = <todo done: false> "Buy milk"
name(task)
task.done
str(task[0])

// ===== Type occurrence annotations =====
type NumberList = int[]
type MaybeString = string?
5 is int
[1, 2, 3] is int[]
null is string?
"hello" is string?
