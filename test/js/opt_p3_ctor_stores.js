// P3: Direct property stores in constructor via js_set_shaped_slot
// Verifies that this.prop = val assignments in constructors correctly
// store values into pre-shaped TypedMap slots.

class Counter {
    constructor(start, step) {
        this.value = start;
        this.step = step;
        this.count = 0;
    }
    tick() {
        this.value = this.value + this.step;
        this.count = this.count + 1;
        return this;
    }
    reset() {
        this.value = 0;
        this.count = 0;
        return this;
    }
}

// Construct and mutate
var c = new Counter(0, 5);
c.tick().tick().tick();     // value = 15, count = 3
var val1 = c.value;
var cnt1 = c.count;

c.reset();
c.tick().tick();            // value = 10, count = 2
var val2 = c.value;
var cnt2 = c.count;

// Multiple independent instances — verify no slot aliasing
var c1 = new Counter(100, 10);
var c2 = new Counter(200, 20);
c1.tick();
c2.tick();
var c1v = c1.value;   // 110
var c2v = c2.value;   // 220

// Class with five properties — exercises all five slots
class Box {
    constructor(x, y, w, h, label) {
        this.x = x;
        this.y = y;
        this.w = w;
        this.h = h;
        this.label = label;
    }
    area() { return this.w * this.h; }
    perimeter() { return 2 * (this.w + this.h); }
}

var box = new Box(10, 20, 6, 4, "rect");
var barea = box.area();      // 24
var bperi = box.perimeter(); // 20
var blabel = box.label;      // "rect"
var bx = box.x;              // 10
var by = box.y;              // 20

{ val1: val1, cnt1: cnt1, val2: val2, cnt2: cnt2, c1v: c1v, c2v: c2v, barea: barea, bperi: bperi, blabel: blabel, bx: bx, by: by };
