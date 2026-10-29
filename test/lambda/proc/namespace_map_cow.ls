// S8.2.2v6: namespace-aware writes detach COW shapes and preserve adjacent fields.
import n: 'urn:lambda:keys'
import alias: 'urn:lambda:keys'
import other: 'urn:lambda:other'
pn main() {
    var m = {a: 2}
    m[n.a] = 1
    m[other.a] = 3
    let snapshot = m
    m[alias.a] = "wide"
    m.n.a = "dotted"
    let spread = {*: m, tail: 9}
    print([m[alias.a], m.a, m[other.a], len(m),
        snapshot[n.a], snapshot.a, snapshot[other.a],
        spread[n.a], spread.a, spread[other.a], spread.tail])
}
