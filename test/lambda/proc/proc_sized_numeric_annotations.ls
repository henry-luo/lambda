// Test explicit sized numeric annotations in procedural variables.

pn main() {
    var a_source = 127
    var a: i8 = i8(a_source)
    var a_next = 128
    a = i8(a_next)
    print("A:")
    print(a)
    print(":")
    print(type(a))

    var b_source = 255
    var b: u8 = u8(b_source)
    var b_next = 1
    b = b + u8(b_next)
    print(" B:")
    print(b)
    print(":")
    print(type(b))

    var b16_source = 0
    var b16: i16 = i16(b16_source)
    var b16_next = 32768
    b16 = i16(b16_next)
    print(" B16:")
    print(b16)
    print(":")
    print(type(b16))

    var c16_source = 65535
    var c16: u16 = u16(c16_source)
    var c16_next = 1
    c16 = c16 + u16(c16_next)
    print(" C16:")
    print(c16)
    print(":")
    print(type(c16))

    var c_source = 0
    var c: i32 = i32(c_source)
    var c_next = 2147483648
    c = i32(c_next)
    print(" C:")
    print(c)
    print(":")
    print(type(c))

    var d_source = 0
    var d: u32 = u32(d_source)
    var d_next = -1
    d = u32(d_next)
    print(" D:")
    print(d)
    print(":")
    print(type(d))

    var e_source = 0
    var e: u64 = u64(e_source)
    e = 18446744073709551615u64
    print(" E:")
    print(e)
    print(":")
    print(type(e))
}
