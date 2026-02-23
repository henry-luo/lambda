// Object type inheritance
type Shape {
    color: string;
    fn describe() => "Shape: " ++ color
}
type Circle : Shape {
    radius: int;
    fn area() => radius * radius * 3
}
let c = {Circle color: "red", radius: 5}

// Field access (inherited + own)
c.color
c.radius

// Method calls (inherited + own)
c.describe()
c.area()

// Type checking with inheritance
c is Circle
c is Shape
c is object
5 is Circle

// Method override
type Animal {
    name: string;
    fn speak() => name ++ " says ..."
}
type Dog : Animal {
    breed: string;
    fn speak() => name ++ " says woof!"
}
let d = {Dog name: "Rex", breed: "Lab"}
d.speak()
1
d.name
1
d.breed
d is Dog
d is Animal
