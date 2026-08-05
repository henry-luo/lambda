// Nullable scalar lanes must not take the plain-scalar equality fast path.
// Equality sees ItemNull after the lane is boxed at the dynamic comparison.
pn main() {
    var values: int[] = [7]
    var absent: int? = values[1]
    var floats: float[] = [1.5]
    var absent_float: float? = floats[1]
    print(string([absent == null, absent != null,
        absent_float == null, absent_float != null]) ++ "\n")
}
