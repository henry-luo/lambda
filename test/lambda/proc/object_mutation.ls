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

type Rect {
    width: float,
    height: float;
    pn scale(factor: float) {
        width = width * factor
        height = height * factor
    }
    fn area() => width * height
}

pn main() {
    // pn methods require a mutable root so their replacement receiver can be installed.
    var c = <Counter count: 0>
    print(c.count); print("\n")
    print(c.increment()); print("\n")
    print(c.count); print("\n")
    print(c.increment()); print("\n")
    print(c.count); print("\n")

    print(c.add(10)); print("\n")
    print(c.count); print("\n")
    print(c.reset()); print("\n")
    print(c.count); print("\n")

    var r = <Rect width: 3.0, height: 4.0>
    print(r.area()); print("\n")
    r.scale(2.0)
    print(r.area()); print("\n")
    print(r.width); print("\n")
    print(1); print("\n")
    print(r.height); print("\n")

    var d = <Counter>
    print(d.count); print("\n")
    print(d.increment()); print("\n")
    print(d.count); print("\n")
}
