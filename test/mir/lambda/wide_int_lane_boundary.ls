// MT fixture: the wide-lane gate. Admission is read from the numeric
// classifier's result cell, not from "an operand is 64-bit wide", so the joins
// below must keep their existing lowering.
//
// This is the negative half of wide_int_native_lane: widening the gate to any
// expression with a 64-bit operand would silently change results, because each
// case here lands in a DIFFERENT domain than the wrapping i64/u64 lane.
//
// Checked by wide_int_lane_boundary.mir-check.

// i64 joined with `int` enters the exact `integer` tower (LAMBDA_NUM_INTEGER),
// which is unbounded — wrapping it at 64 bits would be a wrong answer, not a
// representation change.
fn tower_join(a: i64, b: int) => a + b

// A narrow sized operand is a packed NUM_SIZED Item, not a wide lane. The
// result cell IS i64, so this is the case the gate deliberately declines: its
// operand needs sign/zero extension the lane emitter does not perform.
fn narrow_join(a: i64, b: i32) => a * b

// div/mod are excluded from the classifier's sized-integer cell altogether, so
// they join in the semantic tower regardless of operand width.
fn wide_div(a: i64, b: i64) => a div b

pn main() {
    print(string(tower_join(4611686018427387904i64, 1)) ++ "\n")
    print(string(narrow_join(4611686018427387904i64, 7i32)) ++ "\n")
    print(string(wide_div(4611686018427387904i64, 3i64)) ++ "\n")
}
