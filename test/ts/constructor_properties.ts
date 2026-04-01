// Constructor parameter properties
class Point {
    constructor(public x: number, public y: number) {
    }
}

const p = new Point(3, 4);
console.log(p.x);
console.log(p.y);

// Constructor with mix of param properties and regular params
class Rectangle {
    constructor(public width: number, public height: number, label: string) {
        console.log(label);
    }
}

const r = new Rectangle(10, 20, "rect");
console.log(r.width);
console.log(r.height);
