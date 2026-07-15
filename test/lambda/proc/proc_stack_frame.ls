fn wide() i64 => 9223372036854775807i64

fn make_reader(value) {
    fn read() => value
    read
}

fn make_scalar_map() => map([
    9223372036854775807i64, 9223372036854775806i64,
    t'2026-07-15T12:34:56Z', 9223372036854775805i64
])

pn maybe_wide(ok) i64^ {
    if (ok) { return 9223372036854775807i64 }
    raise error("wide failure")
}

pn delayed_wide() {
    sleep(1)^
    return 9223372036854775805i64
}

pn main() {
    let reader = make_reader(9223372036854775806i64)
    let joined = [wide()] ++ [reader()]
    let mapped = {value: wide()}
    let success^success_err = maybe_wide(true)
    let failed^failure = maybe_wide(false)
    let handle = start delayed_wide()
    let across_wait = [wide(), wait(handle)^, reader()]
    let scalar_map = make_scalar_map()

    print([wide(), reader(), joined[0], joined[1], mapped.value])
    print([success, type(success_err), failed, type(failure)])
    print(across_wait)
    print([
        scalar_map[9223372036854775807i64],
        scalar_map[t'2026-07-15T12:34:56Z']
    ])
}
