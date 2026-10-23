// D2.4/D8.3.3: string admission refines len, without certifying open arrays.
pn forward_text(value) { return value }
pn quote_text(value) { return "[" ++ forward_text(value) ++ "]" }
pn wrap_text(value) { return "{" ++ quote_text(value) ++ "}" }
pn native_rows(text) {
    let n = len(text)
    var previous = fill(n + 1, 0)
    var current = fill(n + 1, 1)
    var i = 0
    while (i <= n) { previous[i] = i; i = i + 1 }
    var saved = previous
    previous = current
    current = saved
    return [previous[n], current[n], previous[-1], current[n + 1]]
}
pn mixed_rows(value) {
    var rows = fill(2, 1)
    var saved = rows
    rows = value
    return [rows[0], rows[-1], saved[0]]
}
pn any_count(value: any | error) { return len(value) }
pn append_alias(value) {
    var result = value
    let snapshot = result
    result = result ++ wrap_text(result)
    return [snapshot, result]
}
pn guarded_step(values: int[], index: int) {
    let value = values[index]
    let step = if (value > 1) value else 1
    return [step, step + 1]
}
pn guarded_error(value: any | error) {
    return if (value > 1) value else 1
}
pn string_key(value) { return value }
pub pn forward_key(value) { return string(string_key(value)) == "x" }
pn main() {
    let dynamic_key = forward_key
    print([forward_key("x"), dynamic_key('x'), dynamic_key(null),
        dynamic_key(3), type(dynamic_key(error("key"))) == error]); print("\n")
    print(native_rows("λab")); print("\n")
    print(native_rows("")); print("\n")
    print([mixed_rows([2.5]), mixed_rows(["x"]), mixed_rows([])]); print("\n")
    print([wrap_text("λ"), append_alias("a")]); print("\n")
    print([any_count("λab"), any_count([1, 2]),
        type(any_count(error("count"))) == error]); print("\n")
    print([guarded_step([3], 0), guarded_step([3], 1), guarded_step([-2], 0),
        guarded_step([int(nan)], 0), guarded_step([int(inf)], 0),
        type(guarded_error(error("guard"))) == error]); print("\n")
}
