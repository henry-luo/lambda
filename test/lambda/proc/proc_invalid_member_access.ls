// S7.1.1v2/S7.1.3v2: invalid member reads are null; invalid writes are hard
// errors. The same source is exercised by MIR Direct and the AST interpreter.
pn write_array(var xs, key, value) int^ {
    xs[key] = value
    return 1
}

pn write_map(var m, key, value) int^ {
    m[key] = value
    return 1
}

pn write_typed_array(var xs: int[], key, value) int^ {
    xs[key] = value
    return 1
}

pn main() {
    var xs = [10, 20]
    print(string([xs["name"], xs[5.5], xs[-1], xs[2]]) ++ "\n")

    var array_name_error = null
    write_array(xs, "name", 99) ^ { array_name_error = ^ }
    var array_fraction_error = null
    write_array(xs, 5.5, 99) ^ { array_fraction_error = ^ }
    print(string([array_name_error is error, array_fraction_error is error,
        xs[0], xs[1]]) ++ "\n")

    var m = {key: 7}
    print(string([m[1], m[5.5], m["missing"]]) ++ "\n")
    var map_key_error = null
    write_map(m, 1, 8) ^ { map_key_error = ^ }
    print(string([map_key_error is error, m.key]) ++ "\n")
    var map_symbol_error = null
    write_map(m, 'key', 8) ^ { map_symbol_error = ^ }
    print(string([map_symbol_error is error, m.key]) ++ "\n")

    var empty_map = map(["", 1])
    var empty_map_error = null
    write_map(empty_map, "", 2) ^ { empty_map_error = ^ }
    print(string([empty_map_error is error, empty_map[""]]) ++ "\n")

    var typed: int[] = [1, 2]
    var typed_key_error = null
    write_typed_array(typed, "bad", 8) ^ { typed_key_error = ^ }
    var typed_exact_float_error = null
    write_typed_array(typed, 1.0, 9) ^ { typed_exact_float_error = ^ }
    var typed_exact_decimal_error = null
    write_typed_array(typed, 0m, 11) ^ { typed_exact_decimal_error = ^ }
    print(string([typed_key_error is error, typed_exact_float_error is error,
        typed_exact_decimal_error is error,
        typed[0], typed[1]]) ++ "\n")

    var el = <div id: "ok", "child">
    print(string([el[99], el["missing"]]) ++ "\n")
    var element_name_error = null
    write_array(el, "id", "changed") ^ { element_name_error = ^ }
    print(string([element_name_error is error, el.id]) ++ "\n")
    var element_fraction_error = null
    write_array(el, 5.5, "new") ^ { element_fraction_error = ^ }
    print(string([element_fraction_error is error, el.id, el[0]]) ++ "\n")
}
