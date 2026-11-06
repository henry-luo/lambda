// D8.2.4/S12.3.3v2: nested types preserve each enclosing assembly cursor.
fn make(seed: int) {
    type Outer {
        value: int,
        fn nested() {
            type Inner {
                item: int,
                fn read() => item
                fn deeper() {
                    type Leaf {
                        leaf: int,
                        fn read() => leaf
                    }
                    return <Leaf leaf:item>
                }
                fn after() => item + 1
            }
            return <Inner item:value>
        }
        tail: float,
        fn after() => value + tail
    }
    return <Outer value:seed, tail:0.5>
}
let first = make(5)
let second = make(7)
let inside = first.nested();
[
    inside.read() == 5,
    inside.deeper().read() == 5,
    inside.after() == 6,
    first.after() == 5.5,
    second.after() == 7.5,
    first.tail == 0.5
]
