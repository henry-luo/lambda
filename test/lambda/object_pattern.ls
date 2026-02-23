// Pattern matching with object types
type Shape {
    color: string = "black"
}
type Circle : Shape {
    radius: float
}
type Rect : Shape {
    width: float,
    height: float
}

fn describe(s) => match s {
    case Circle: "circle"
    case Rect: "rect"
    case Shape: "shape"
    default: "unknown"
}

let c = {Circle color: "red", radius: 5.0}
let r = {Rect color: "blue", width: 3.0, height: 4.0}
describe(c)
1
describe(r)
1
describe(42)

// Inheritance: Circle is also a Shape
fn is_shape(s) => match s {
    case Shape: true
    default: false
}
is_shape(c)
1
is_shape(r)
1
is_shape("hello")
