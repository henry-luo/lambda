// Tune17 T1: inferred and declared recursive procedures publish the same int lane.

pn tune17_tak_inferred(x, y, z) {
    if (y >= x) {
        return z
    }
    var a = tune17_tak_inferred(x - 1, y, z)
    var b = tune17_tak_inferred(y - 1, z, x)
    var c = tune17_tak_inferred(z - 1, x, y)
    return tune17_tak_inferred(a, b, c)
}

pn tune17_tak_declared(x: int, y: int, z: int) int {
    if (y >= x) {
        return z
    }
    var a: int = tune17_tak_declared(x - 1, y, z)
    var b: int = tune17_tak_declared(y - 1, z, x)
    var c: int = tune17_tak_declared(z - 1, x, y)
    return tune17_tak_declared(a, b, c)
}

pn main() {
    print(tune17_tak_inferred(8, 6, 3))
    print(" ")
    print(tune17_tak_declared(8, 6, 3))
    print("\n")
}
