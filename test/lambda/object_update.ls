// Object update syntax: {Type source, field: newValue}
type Point {
    x: float,
    y: float
}

// Basic update — override one field
let p = <Point x: 1.0, y: 2.0>
let q = <Point p, x: 10.0>
q.x
1
q.y

// Override all fields
let r = <Point p, x: 100.0, y: 200.0>
r.x
1
r.y

// No override — copy all fields
let s = <Point p>
s.x
1
s.y

// Inheritance + update
type Shape {
    color: string = "black"
}
type Circle : Shape {
    radius: int
}
let c1 = <Circle color: "red", radius: 5>
let c2 = <Circle c1, radius: 10>
c2.color
1
c2.radius

// Update with defaults — source fills non-overridden, non-default fields
type Config {
    host: string = "localhost",
    port: int = 8080,
    debug: bool = false
}
let cfg1 = <Config host: "api.io", port: 443, debug: true>
let cfg2 = <Config cfg1, host: "new.io">
cfg2.host
1
cfg2.port
1
cfg2.debug

// ~ self-reference in method returns updated object
type Vec {
    x: float,
    y: float;
    fn translate(dx: float, dy: float) => <Vec ~, x: x + dx, y: y + dy>
    fn scale(factor: float) => <Vec ~, x: x * factor, y: y * factor>
}
let v = <Vec x: 3.0, y: 4.0>
let v2 = v.translate(1.0, -1.0)
v2.x
1
v2.y
let v3 = v.scale(2.0)
v3.x
1
v3.y
