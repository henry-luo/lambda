// MT fixture: `+ - *` on two wide operands lower to machine add/sub/mul.
//
// i64/uint64 have no inline Item form at any magnitude (D2.2.3), so before
// this lane existed every such operation boxed both operands, called
// fn_add/fn_sub/fn_mul, and let pack_sized_integer push a number home for a
// result the next instruction unboxed again. Three side-stack words per
// operation, none of them reclaimed inside a loop body — which is why the
// accumulators below used to exhaust the side stack (D5.2.3 / DO24) rather
// than merely run slow.
//
// The classifier's I64/U64 result cell wraps in two's complement
// (LAMBDA_NUM_OVERFLOW_SIZED_WRAP), and add/sub plus the low half of a
// multiply are bit-identical for signed and unsigned, so one opcode set
// serves both lanes.
//
// Checked by wide_int_native_lane.mir-check.

fn wide_i64(a: i64, b: i64) i64 => a * b + a - b

fn wide_u64(a: u64, b: u64) u64 => a * b + a - b

// A declared wide local is the loop-carried case: the store must stay in the
// lane too, or every iteration re-acquires a home it never gives back.
pn accumulate_i64(n: int) {
    var acc: i64 = 0i64
    var i = 0
    while (i < n) {
        acc = acc + 1103515245i64 * 12345i64
        i = i + 1
    }
    return acc
}

pn accumulate_u64(n: int) {
    var acc: u64 = 0u64
    var i = 0
    while (i < n) {
        acc = acc + 6364136223846793005u64 * 3u64
        i = i + 1
    }
    return acc
}

pn main() {
    print(string(wide_i64(4611686018427387904i64, 3i64)) ++ "\n")
    print(string(wide_u64(18446744073709551615u64, 2u64)) ++ "\n")
    print(string(accumulate_i64(4)) ++ "\n")
    print(string(accumulate_u64(4)) ++ "\n")
}
