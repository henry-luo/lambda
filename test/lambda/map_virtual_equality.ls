// S5.4.1 / D7.4.5v2: key-unordered map equality ignores the storage carrier.
let native = {a: 1, b: null, nested: {x: [2, 3]}}
let virtual = map(["nested", map(["x", [2, 3]]), 'b', null, "a", 1.0])
type Named { a: int }
let nominal = <Named a: 1>
let checks = {
    native_virtual: native == virtual, virtual_native: virtual == native,
    nested_arrays: [native] == [virtual],
    key_order: map(["b", 2, "a", 1]) == {a: 1, b: 2},
    empty: map() == {} and {} == map(),
    numeric_keys: map([1, "a"]) == map([1.0, "a"]),
    name_keys: map(["a", null]) == map(['a', null]),
    missing_null: map(["a", null]) != map(["b", null]),
    missing_null_native: map(["a", null]) != {b: null},
    null_present: map(["a", null]) == {a: null},
    extra_key: map(["a", 1, "b", null]) != {a: 1},
    integer_name: map([1, null]) != {'1': null},
    unequal_value: map(["a", 1]) != {a: 2},
    nominal_gate: nominal != map(["a", 1]) and map(["a", 1]) != nominal,
    poison: map(["a", nan]) != {a: nan},
    distinct_family: map() != [] and map() != <p>
};
[for (label, passed in checks where passed != true) string(label)]
