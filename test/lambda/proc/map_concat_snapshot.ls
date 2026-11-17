// S9.1.2 and S10.6.1v2: merging and later writes retain source snapshots.
pn main() {
    var left = {a: 1, child: {x: 1}}
    var right = {b: 2}
    var merged = (left ++ right) ^ { null }
    left.a = 9
    right.b = 8
    merged.child.x = 7
    print([left.a, right.b, merged.a, merged.b, left.child.x, merged.child.x])
    print("\n")

    var identity = (left ++ null) ^ { null }
    identity.a = 10
    print([left.a, identity.a])
    print("\n")
}
