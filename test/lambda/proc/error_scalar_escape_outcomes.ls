// S7.7.4v2/D6.1.3/D6.2.3: publishing a new outcome preserves scalar snapshots.
fn identity(value: any) any => value
fn checked(value) float^ {
    let admitted: float = value
    admitted^
}
pn replace(var target: any, value: any) { target = value }
pn main() {
    var scalar: any = checked(1e-310)
    let saved = scalar
    scalar = checked("bad")
    let failed = scalar
    scalar = checked(2e-310)
    var borrowed: any = 0
    replace(borrowed, 1e-310)
    let saved_borrowed = borrowed
    replace(borrowed, 2e-310)
    let signed = identity(9223372036854775807i64)
    let unsigned = identity(18446744073709551615u64)
    let small_signed = identity(1i64)
    let small_unsigned = identity(1u64);
    [saved == 1e-310, failed is error, scalar == 2e-310,
     saved_borrowed == 1e-310, borrowed == 2e-310,
     signed == 9223372036854775807i64, type(signed) == i64,
     unsigned == 18446744073709551615u64, type(unsigned) == u64,
     type(small_signed) == i64, type(small_unsigned) == u64]
}
