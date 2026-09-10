// S7.1.3v2/S9.1.2/D5.3.4: misses and operand calls preserve checked ownership.
pn byte_store(index: int, value: any) {
    var a: i8[] = [1i8]
    a[index] = value
    return a
}
pn replace(var a: i64[]) int {
    a = [11i64, 12i64]
    return 1
}
pn ordered() {
    var a: i64[] = [1i64, 2i64]
    a[replace(a)] = 9223372036854775807i64
    return a
}
pn sliced() {
    var a: i8[] = [1i8, 2i8, 3i8]
    var part: i8[] = slice(a, 1, 3)
    part[0] = 9i8
    return [a, part]
}
pn main() {
    var rejected = 0
    byte_store(4294967296, 2i8) ^ { rejected = rejected + 1 }
    byte_store(-4294967296, 2i8) ^ { rejected = rejected + 1 }
    byte_store(0, 128u16) ^ { rejected = rejected + 1 }
    print([byte_store(0, 10), byte_store(0, 12u16), rejected, ordered(), sliced()])
}
