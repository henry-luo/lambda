// Regression coverage for the type-directed number model.
pn emit(value) {
    print(type(value))
    print(":")
    print(value)
    print("\n")
}

fn add_one(value: u64) => value + 1

pn main() {
    // a run-time operand: a literal-only overflow is a compile error (S4.1.5)
    let i8_max = 127i8
    emit(i8_max + 1i8)
    emit(1i8 + 1u8)
    emit(1i32 + 1u32)
    emit(1i64 + 1u64)
    emit(255u8 + 1)
    emit(1 + 255u8)
    emit(9223372036854775807u64 + 1)
    emit(9223372036854775808u64 + 1)
    emit(18446744073709551615u64 + 1)
    emit(1i64 + 0.5)
    emit(0.5 + 1u64)
    emit(7i8 / 2u8)
    emit(7i64 / 2u64)
    emit(7i64 div 2)
    emit(bxor(1u8, 3))
    emit(shl(1i64, 1))
    emit(add_one(18446744073709551615u64))
}
