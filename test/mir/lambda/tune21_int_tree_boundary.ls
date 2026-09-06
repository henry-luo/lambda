// T21-2e: an untyped pn parameter carries the implicit `any \ error`
// contract, so a call site admits every argument whose carrier is `any`
// with lambda_type_check. A closed `+ - *` int tree over int-lane leaves
// (an element read with an int witness and an int key, a counter local, a
// literal) cannot produce an error Item, so that check is redundant and is
// no longer emitted. The golden pins that the value still arrives intact.
pn id(x) { return x }
pn min2(a, b) {
    if (a < b) { return a }
    return b
}
pn main() {
    var row = fill(4, 5)
    var i = 1
    print(id(row[i] + 1)); print(" ")
    print(min2(row[i] * 2 - 1, row[i + 1] + 7)); print("\n")
}
