// P4: Direct property reads for typed class instances via js_get_shaped_slot
// When a variable is declared as `var x = new Foo(...)`, property reads
// on x use direct slot-indexed access instead of hashmap lookup.

class Point {
    constructor(x, y) {
        this.x = x;
        this.y = y;
    }
    distSq() {
        return this.x * this.x + this.y * this.y;
    }
    translate(dx, dy) {
        this.x = this.x + dx;
        this.y = this.y + dy;
        return this;
    }
}

// Direct property reads on typed instance (P4 path: p1.x, p1.y)
var p1 = new Point(3, 4);
var p2 = new Point(1, 2);

var d1 = p1.distSq();    // 9 + 16 = 25
var d2 = p2.distSq();    // 1 + 4 = 5
var px = p1.x;           // 3
var py = p1.y;           // 4

// Mutate via method, then re-read (P4 reads after non-constructor assignment)
p1.translate(1, 1);      // p1 becomes (4, 5)
var d3 = p1.distSq();    // 16 + 25 = 41
var prx = p1.x;          // 4
var pry = p1.y;          // 5

// Cross-instance property arithmetic (P4 on both p1 and p2)
var sum_x = p1.x + p2.x; // 4 + 1 = 5
var sum_y = p1.y + p2.y; // 5 + 2 = 7

// String property reads on typed instance
class Person {
    constructor(name, age) {
        this.name = name;
        this.age = age;
    }
    greet() {
        return "Hi, " + this.name;
    }
}

var alice = new Person("Alice", 30);
var bob = new Person("Bob", 25);
var age_diff = alice.age - bob.age;  // 5
var greeting = alice.greet();         // "Hi, Alice"
var aname = alice.name;               // "Alice"

{ d1: d1, d2: d2, px: px, py: py, d3: d3, prx: prx, pry: pry, sum_x: sum_x, sum_y: sum_y, age_diff: age_diff, greeting: greeting, aname: aname };
