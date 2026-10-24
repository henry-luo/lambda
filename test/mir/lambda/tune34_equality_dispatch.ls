// D2.2.2/S9: exclusive dispatch needs an unchanged local and distinct total predicates.
pn choose(key: int) {
    var result = 0
    if (key == 11) { result = result + 2 }
    if (key == 22) { result = result + 3 }
    if (key == 33) { result = result + 5 }
    if (key == 44) { return 8 }
    return result
}
pn rebinding() {
    var key = 11
    var result = 0
    if (key == 11) { result = result + 2; key = 22 }
    if (key == 22) { result = result + 3 }
    if (key == 33) { result = result + 5 }
    return result
}
pn duplicates(key: int) {
    var result = 0
    if (key == 11) { result = result + 2 }
    if (key == 22) { result = result + 3 }
    if (key == 33) { result = result + 5 }
    if (key == 11) { result = result + 7 }
    return result
}
pn arrays(key: int) {
    var a = [1, 2]
    let snapshot = a
    if (key == 11) { a[0] = 3 }
    if (key == 22) { a[0] = 5 }
    if (key == 33) { a[0] = 7 }
    return [a, snapshot]
}
pn from_character(text: string) {
    var key = ord(text)
    var result = 0
    if (key == 65) { result = 2 }
    if (key == 66) { result = 3 }
    if (key == 67) { result = 5 }
    return result
}
pn main() {
    print([choose(11), choose(22), choose(33), choose(44), choose(0)]); print("\n")
    print([from_character("A"), from_character("B"), from_character("C"), from_character(""), from_character("λ")]); print("\n")
    print([rebinding(), duplicates(11), duplicates(22)]); print("\n")
    print([arrays(11), arrays(22), arrays(33), arrays(0)]); print("\n")
}
