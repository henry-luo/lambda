// nullable conversion preserves ordinary errors and numeric poison (S4.1.2, S7.7.2).
// Full MIR currently loses nullable int conversion failures; interpreter/AUTO match this golden.
fn as_int(value: int?) any { return int(value) }
fn from_float(value: float?) any { return int(value) }
fn widen_optional(value: int?) any { return float(value) }
fn same_float(value: float?) any { return float(value) }
// literal contracts must retain the noncollecting numeric path too (D2.3.5).
fn float_literals() float { return float(3) + float(2.5) }
fn int_literals() int { return int(3) + int(2.75) }
pn store_cast(var values: int[], value: int?) any { values[0] = int(value); return null }
fn failed(value: any) bool { return match value { case error: true default: false } }
pn main() {
    print([float_literals(), int_literals()]); print("\n")
    print([as_int(7), from_float(-2.75), widen_optional(7), same_float(-2.75)]); print("\n")
    print([failed(as_int(null)), failed(from_float(null)),
        failed(widen_optional(null)), failed(same_float(null))]); print("\n")
    print([as_int(int(inf)), as_int(int(-inf)), as_int(int(nan)),
        widen_optional(int(inf)), widen_optional(int(-inf)), widen_optional(int(nan))]); print("\n")
    print([from_float(inf), from_float(-inf), from_float(nan), same_float(nan)]); print("\n")
    var values: int[] = [3]
    let snapshot = values
    store_cast(values, 8)
    let bad = store_cast(values, null)
    print([snapshot[0], values[0], failed(bad)]); print("\n")
}
