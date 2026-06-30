// Test explicit sized numeric annotations in procedural variables.

pn main() {
    var a: i8 = 127
    a = 128
    print("A:")
    print(a)
    print(":")
    print(type(a))

    var b: u8 = 255
    b = b + 1
    print(" B:")
    print(b)
    print(":")
    print(type(b))

    var b16: i16 = 0
    b16 = 32768
    print(" B16:")
    print(b16)
    print(":")
    print(type(b16))

    var c16: u16 = 65535
    c16 = c16 + 1
    print(" C16:")
    print(c16)
    print(":")
    print(type(c16))

    var c: i32 = 0
    c = 2147483648
    print(" C:")
    print(c)
    print(":")
    print(type(c))

    var d: u32 = 0
    d = -1
    print(" D:")
    print(d)
    print(":")
    print(type(d))

    var e: u64 = 0
    e = 18446744073709551615u64
    print(" E:")
    print(e)
    print(":")
    print(type(e))
}
