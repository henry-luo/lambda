// record lanes, snapshots and nullable native returns (S9.1.2, D5.3).
type Node = {value: int, next: Node?}
type Counter = {count: int, flag: bool}
type Scalars = {i: int?, f: float?, b: bool?, text: string?, next: Node?}

fn scalar_record(yes: bool) Scalars {
    return {i: if (yes) 7 else null, f: if (yes) 1.5 else null,
        b: if (yes) true else null, text: if (yes) string(17) else null, next: chain(20)}
}

fn chain(n: int) Node? {
    if (n == 0) { return null }
    return {value: n, next: chain(n - 1)}
}

fn total(node: Node?) int {
    var cursor = node
    var sum: int = 0
    while (cursor != null) {
        sum = sum + cursor.value
        cursor = cursor.next
    }
    return sum
}

pn bump(var counter: Counter, amount: int) any {
    counter.count = counter.count + amount
    counter.flag = not counter.flag
}

fn checked(value) int { return value }

pn main() {
    var counter: Counter = {count: 2, flag: false}
    let observer: Counter = counter
    bump(counter, 5)
    print(observer.count); print(" "); print(counter.count); print(" ")
    print(observer.flag); print(" "); print(counter.flag); print("\n")
    let nodes = chain(20)
    print(total(nodes)); print(" "); print(total(null)); print("\n")
    let values = [counter, observer, null, "two", 1]
    print(values[0].count); print(" "); print(values[1].count); print("\n")
    let bad = checked("bad")
    let good = checked(9)
    print(match bad { case error: 0 case int: 1 }); print(" ")
    print(match good { case error: 0 case int: good }); print("\n")
    var sum: int = 0
    var data: int[] = [3, 5, 7]
    for i of data { sum = sum + data[i] }
    var empty: int[] = []
    for i of empty { sum = -1 }
    print(sum); print(" "); print(counter.count); print("\n")
    print(chain(0)); print("\n")
    // native scalars and a dynamic string survive the later recursive allocation.
    let present = scalar_record(true)
    let absent = scalar_record(false)
    print(present.i); print(" "); print(present.f); print(" "); print(present.b); print(" ")
    print(present.text); print(" "); print(total(present.next)); print("\n")
    print(absent.i); print(" "); print(absent.f); print(" "); print(absent.b); print(" ")
    print(absent.text); print(" "); print(total(absent.next)); print("\n")
}
