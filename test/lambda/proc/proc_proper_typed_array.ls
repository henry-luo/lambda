// Proper T[] regression: dynamic record arrays must be admitted into the
// named record layout before a typed consumer takes a direct field read.
fn dynamic(value) any { value }

type Variable = {value: int}
type VariableStore = {values: Variable[]}
type IndexedVariable = {constraints: int[]}
type IndexedStore = {values: IndexedVariable[]}

pn sum_values(values: Variable[]) int {
    return values[0].value + values[1].value
}

pn sum_ints(values: int[]) int {
    return values[0] + values[1]
}

pn bump_first(var store: VariableStore) any {
    store.values[0].value = store.values[0].value + 1
}

pn append_value(var store: VariableStore) any {
    push(store.values, {value: 22})
}

pn append_value_forward(var store: VariableStore) any {
    append_value(store)
}

pn append_constraint(var store: IndexedStore) any {
    var constraints: int[] = store.values[0].constraints
    append_int(constraints, 7)
    store.values[0].constraints = constraints
}

pn append_int(var values: int[], value: int) any {
    push(values, value)
}

pn fresh_ints() int[] {
    return []
}

fn fresh_expression_ints() int[] => []

pn main() {
    let source = [{value: 4.0}, {value: 5.0}]
    var values: Variable[] = dynamic(source)
    push(values, dynamic({value: 6.0}))
    var rejected_record = null
    push(values, dynamic({strength: 10})) ^ { rejected_record = ^ }
    print(sum_values(values))
    print("\n")

    // A record-pointer transform stages boxed Items; the declared result must
    // re-admit `Variable[]` before its direct packed field read.
    var reversed_values: Variable[] = reverse(values)
    print(reversed_values[0].value)
    print("\n")

    // These writes historically routed compact lanes through the int64 alias,
    // corrupting adjacent bytes. Each dynamic value forces the checked path.
    var bytes: u8[] = [1u8, 2u8, 3u8]
    var shorts: i16[] = [10i16, 20i16]
    var fractions: f32[] = [1.5f32, 2.5f32]
    bytes[1] = dynamic(9u8)
    shorts[0] = dynamic(-7i16)
    fractions[1] = dynamic(3.5f32)
    push(bytes, dynamic(7u8))

    var rejected = null
    push(bytes, dynamic(300)) ^ { rejected = ^ }
    print(string([values[2].value, bytes[0], bytes[1], bytes[2], bytes[3],
        shorts[0], shorts[1], fractions[0], fractions[1], rejected is error,
        rejected_record is error]) ++ "\n")

    // T[] accepts range sources by materializing each item through the same
    // checked boundary, then rebuilding the declared native lane.
    var ranged: int[] = (2 to 4)
    var dynamic_ints: int[] = dynamic([12, 13])
    var returned_ints: int[] = fresh_ints()
    var expression_ints: int[] = fresh_expression_ints()
    push(returned_ints, dynamic(15))
    push(expression_ints, dynamic(16))
    var reversed: u8[] = reverse(bytes)
    push(reversed, dynamic(11u8))
    print(string([ranged[0], ranged[2], sum_ints(dynamic_ints), reversed[0],
        reversed[4], returned_ints[0], expression_ints[0]]) ++ "\n")

    // Nullable scalar lanes remain typed Array storage and retain null on a
    // checked push rather than demoting the declared array to boxed items.
    var optional: int?[] = dynamic([1, null])
    optional[0] = dynamic(4)
    push(optional, dynamic(null))
    print(string([optional[0], optional[1] is null, optional[2] is null]) ++ "\n")

    // Nested array rank remains part of the contract rather than collapsing
    // to Array<map>. Inner maps are reified before their field reads.
    var nested: Variable[][] = dynamic([[{value: 8.0}], [{value: 9.0}]])
    nested[0][0] = dynamic({value: 10.0})
    print(string([nested[0][0].value, nested[1][0].value]) ++ "\n")

    // Nullable named-record arrays retain their concrete MapVariable* lane;
    // a null reservation neither boxes the array nor weakens later reification.
    var nullable_values: Variable?[] = dynamic([null, {value: 12.0}])
    nullable_values[0] = dynamic({value: 11.0})
    print(string([nullable_values[0].value, nullable_values[1].value]) ++ "\n")

    // A typed map root may own a typed record array. This is the same deep
    // write-through shape used by the DeltaBlue planner's record stores.
    var store: VariableStore = {values: [{value: 20.0}]}
    bump_first(store)
    append_value(store)
    append_value_forward(store)
    print(string([store.values[0].value, store.values[1].value,
        store.values[2].value]) ++ "\n")

    var indexed_store: IndexedStore = {values: [{constraints: []}]}
    append_constraint(indexed_store)
    print(string(indexed_store.values[0].constraints) ++ "\n")

    // A typed mask write stages all selected values before it touches the
    // compact payload; the dynamic RHS therefore cannot bypass int[] checks.
    var masked: int[] = [1, 2, 3, 4]
    let mask = masked gt 2
    masked[mask] = dynamic(9)
    print(string(masked) ++ "\n")

    // Multi-coordinate writes use the same declared element contract after a
    // reshape has changed the physical ArrayNum rank.
    var matrix: int[] = reshape([1, 2, 3, 4], [2, 2])
    matrix[1, 0] = dynamic(8)
    print(string([matrix[0, 1], matrix[1, 0]]) ++ "\n")
    print(string([masked is int[], matrix is int[], masked is string[]]) ++ "\n")

    // Reordering and joins retain a compatible certificate when they only
    // copy already-admitted leaves into a fresh exact numeric carrier.
    var left: int[] = [5, 6]
    var right: int[] = dynamic([7, 8])
    var joined: int[] = concat(left, right)
    joined[3] = dynamic(9)
    print(string(joined) ++ "\n")

    // Elements expose the same ordered sequence face as arrays. Admission
    // materializes its content into the exact compact carrier for `int[]`.
    var element_values: int[] = dynamic(<elmt 30; 31>)
    print(string(element_values) ++ "\n")

    // Pointer lanes are raw String* slots, not Item words. This declaration
    // exercises the certificate-guarded direct reader and its boxed result.
    var labels: string[] = dynamic(["east", "west"])
    print(labels[1] ++ "\n")

    // Reordering a pointer lane may not retain a raw-pointer certificate on a
    // generic Item result; the destination boundary must rebuild the lane.
    var reversed_labels: string[] = reverse(labels)
    print(reversed_labels[0] ++ "\n")
}
