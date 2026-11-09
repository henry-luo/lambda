// S7.1.1v3/D6.3: admitted scalar reads preserve absence without a collecting fallback.
fn float_at(values: float[], key: int) any => values[key]
fn int_at(values: int[], key: int) any => values[key]
fn bool_at(values: bool[], key: int) any => values[key]
fn literal_fields() any => {ints: [1, 2, 3], floats: [0.0, 1e-310], flags: [true, false]}
pn update_literal(var record) any { record.ints[0] = 9; return record }
pn main() {
    var floats: float[] = [-0.0, 1e-310, math.sqrt(-1.0)]
    print([1.0 / float_at(floats, 0) < 0.0, float_at(floats, 1) == 1e-310,
        float_at(floats, 2) != float_at(floats, 2)]); print("\n")
    print([float_at(floats, -1), float_at(floats, 3), float_at(floats, int(math.sqrt(-1.0)))]); print("\n")
    var ints: int[] = [3, 5, 7]
    var flags: bool[] = [true, false]
    print([int_at(ints, 1), int_at(ints, -1), int_at(ints, 3)]); print("\n")
    print([bool_at(flags, 0), bool_at(flags, 1), bool_at(flags, 2)]); print("\n")
    var empty: float[] = fill(0, 1.5)^
    print(float_at(empty, 0)); print("\n")
    var matrix: float[][] = [[1.5, 2.5], [3.5, 4.5]]
    let row = transpose(matrix)[0]
    print([float_at(row, 0), float_at(row, 1), float_at(row, 2)]); print("\n")
    var first = literal_fields()
    let second = literal_fields()
    update_literal(first)^
    print([first.ints, second.ints, second.floats[1] == 1e-310, second.flags]); print("\n")
}
