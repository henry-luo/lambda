// S11.4.10/D3.2.4v4: admission keeps open fields and the declared layout.
type Point = {x: int, label: string}
type Node = {point: Point, next: Node?}
type Named { x: int, fn twice() => x * 2 }
type Derived : Named { label: string }

fn read_point(p: Point) => p.x + len(p.label)
fn identity(v) => v
fn read_named(p: Named) => p.x
fn admit_node(v: any) Node => v

pn main() {
    var p: Point = identity({extra: "kept", label: "abc", x: 4})
    let snapshot = p
    var total = 0
    var i = 0
    while (i < 100) {
        total = total + read_point(p)
        i = i + 1
    }
    print([total, p.x, p.label, p.extra])
    print("\n")
    p.more = 9
    p.x = 6
    print([read_point(p), p.extra, p.more, snapshot.x])
    print("\n")
    let ordered: Point = identity({x: 4, label: "abc", extra: "ordered"})
    let exact: Point = identity({x: 4, label: "abc"})
    total = 0
    i = 0
    while (i < 100) {
        total = total + read_point(ordered) + read_point(exact)
        i = i + 1
    }
    print([total, ordered.extra])
    print("\n")
    var node: Node = identity({extra: true, next: null, point: p})
    let before = node
    node.point = identity({label: "xy", x: 10, extra: "new"})
    print([read_point(node.point), node.point.extra, before.point.x, node.extra])
    print("\n")
    var named: Named = <Named x: 3>
    named.extra = "kept"
    print([read_named(named), named.twice(), named is Named, named.extra])
    print("\n")
    let derived = <Derived x: 5, label: "derived">
    print([read_named(derived), derived is Derived, derived is Named, derived.label])
    print("\n")
    var raw = identity(node)
    raw.point = identity({x: "bad", label: "x"})
    print([raw.point.x, node.point.x, node.point.extra])
    print("\n")
    let rejected = admit_node(raw) ^ { print("rejected\n") }
}
