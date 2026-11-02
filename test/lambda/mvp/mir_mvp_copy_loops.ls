// S7.1.1v3, S9.1.2: checked misses and source-order overlap survive loop versioning.
pn copy_range(var dst: int[], src: int[], first: int, start: int, end: int) int {
    var i: int = first
    var j: int = start
    while (i < end) { dst[j] = src[i]; i = i + 1; j = j + 1 }
    return i + j
}
pn shift(var a: int[], first: int, start: int, end: int) int {
    var i: int = first
    var j: int = start
    while (i < end) { a[j] = a[i]; i = i + 1; j = j + 1 }
    return i + j
}
pn bool_copy(var dst: bool[], src: bool[], n: int) {
    var i: int = 0
    while (i < n) { dst[i] = src[i]; i = i + 1 }
}
pn adjacent(var a: int[], n: int) int {
    var i: int = 0
    while (i < n) { a[i] = a[i + 1]; i = i + 1 }
    return i
}
pn overlapping(var a: int[], first: int, n: int) int {
    var i: int = first
    while (i < n) { a[i + 1] = a[i]; i = i + 1 }
    return i
}
pn main() {
    var src: int[] = [1, 2, 3, 4]
    var dst: int[] = fill(5, 0)
    print(copy_range(dst, src, 1, 2, 4)); print(" "); print(dst); print("\n")
    let saved: int[] = dst
    print(copy_range(dst, src, 0, 0, 2)); print(" "); print(dst); print(" "); print(saved); print("\n")
    var a: int[] = [1, 2, 3, 4]
    print(shift(a, 0, 1, 3)); print(" "); print(a); print("\n")
    var b: int[] = [1, 2, 3, 4]
    print(shift(b, 1, 0, 4)); print(" "); print(b); print("\n")
    print(copy_range(dst, src, -1, 0, 2)); print(" "); print(dst); print("\n")
    print(copy_range(dst, src, 2, 4, 5)); print(" "); print(dst); print("\n")
    print(copy_range(dst, src, 4, 1, 2)); print(" "); print(dst); print("\n")
    var bits: bool[] = fill(3, false)
    bool_copy(bits, [true, false, true], 3)
    print(bits); print("\n")
    var left: int[] = [1, 2, 3, 4]
    print(adjacent(left, 3)); print(" "); print(left); print("\n")
    let saved_left: int[] = left
    print(adjacent(left, 2)); print(" "); print(left); print(" "); print(saved_left); print("\n")
    print(adjacent(left, 4)); print(" "); print(left); print("\n")
    var right: int[] = [1, 2, 3, 4]
    print(overlapping(right, 0, 3)); print(" "); print(right); print("\n")
    print(overlapping(right, -1, 2)); print(" "); print(right); print("\n")
    print(adjacent(left, -1)); print(" "); print(left); print("\n")
}
