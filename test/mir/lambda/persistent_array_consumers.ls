// D3.3.3v3/S11.4.10: transformation proofs include the actual slot representation.
type Cell = {value: int}
type One = 1

pn sorted_unique(values: string[]) string[] {
    return unique(sort(values))
}

pn admit_strings(value) string[] { return value }
pn admit_ints(value) int[] { return value }
pn admit_pair(value) int[2] { return value }
pn admit_array(value) array { return value }
pn admit_map(value) map { return value }
pn optional_int(value) int? { return value }
pn optional_float(value) float? { return value }
pn literal_int(value) One { return value }

pn reverse_cells(values: Cell[]) Cell[] { return reverse(values) }
pn reverse_nullable(values: float?[]) float?[] { return reverse(values) }

pn main() {
    var words: string[] = ["b", "a", "b"]
    let snapshot = words
    print(sorted_unique(words)); print("\n")
    print(sorted_unique([])); print("\n")
    var changed = sort(words)
    changed[0] = 99
    print([admit_strings(changed) is error, words[0], snapshot[0]]); print("\n")
    print([admit_ints([[1]]) is error, admit_strings([1]) is error]); print("\n")
    var counted: int[2] = [1, 1]
    print([admit_pair(unique(counted)) is error, admit_pair(reverse(counted))]); print("\n")
    print([admit_array([1, "two", null]), admit_map({x: 3})]); print("\n")
    print([admit_array(3) is error, admit_map([1]) is error]); print("\n")
    print([optional_int(null), optional_int(7), optional_int("bad") is error]); print("\n")
    print([optional_float(null), optional_float(7) is float, literal_int(2) is error]); print("\n")
    let cells: Cell[] = [{value: 1}, {value: 2}]
    let reversed: Cell[] = reverse_cells(cells)
    print([reversed[0].value, reversed[1].value, cells[0].value]); print("\n")
    print(reverse_nullable([1.0, null, 2.0])); print("\n")
}
