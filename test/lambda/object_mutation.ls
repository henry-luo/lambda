// pn mutation methods
type Counter {
    count: int = 0;
    pn increment() {
        count = count + 1
    }
    pn add(n: int) {
        count = count + n
    }
    pn reset() {
        count = 0
    }
}

// basic mutation
let c = {Counter count: 0}
c.count
c.increment()
c.count
c.increment()
c.count

// mutation with parameter
c.add(10)
c.count

// reset
c.reset()
c.count

// pn with multiple fields
type Rect {
    width: float,
    height: float;
    pn scale(factor: float) {
        width = width * factor
        height = height * factor
    }
    fn area() => width * height
}
let r = {Rect width: 3.0, height: 4.0}
r.area()
r.scale(2.0)
r.area()
r.width
1
r.height
