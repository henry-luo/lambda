fn wide() i64 => 9223372036854775807i64
fn uwide() u64 => 18446744073709551615u64
fn tiny() float => 5e-324

fn make_reader(value) {
    fn read() => value
    read
}

fn make_scalar_map() => map([
    9223372036854775807i64, 9223372036854775806i64,
    18446744073709551615u64, 18446744073709551614u64,
    t'2026-07-15T12:34:56Z', 9223372036854775805i64
])

pn maybe_wide(ok) i64^ {
    if (ok) { return 9223372036854775807i64 }
    raise error("wide failure")
}

pn maybe_uwide(ok) u64^ {
    if (ok) { return 18446744073709551615u64 }
    raise error("u64 failure")
}

pn delayed_wide() {
    sleep(1)^
    return 9223372036854775805i64
}

pn main() {
    var loop_i = 0
    var latest = 0i64
    var latest_u = 0u64
    while (loop_i < 1000) {
        latest = wide()
        wide()
        latest_u = uwide()
        uwide()
        loop_i = loop_i + 1
    }
    let reader = make_reader(9223372036854775806i64)
    let ureader = make_reader(uwide())
    let indirect = wide
    let joined = [wide()] ++ [reader()]
    let mapped = {value: wide()}
    let umapped = {value: uwide()}
    let success^success_err = maybe_wide(true)
    let failed^failure = maybe_wide(false)
    let usuccess^usuccess_err = maybe_uwide(true)
    let ufailed^ufailure = maybe_uwide(false)
    let handle = start delayed_wide()
    let across_wait = [wide(), wait(handle)^, reader()]
    let scalar_map = make_scalar_map()

    print([wide(), reader(), joined[0], joined[1], mapped.value])
    print([uwide(), ureader(), umapped.value])
    print([success, type(success_err), failed, type(failure)])
    print([usuccess, type(usuccess_err), ufailed, type(ufailure)])
    print(across_wait)
    print([
        scalar_map[9223372036854775807i64],
        scalar_map[18446744073709551615u64],
        scalar_map[t'2026-07-15T12:34:56Z']
    ])
    print([latest, latest_u, indirect(), tiny(), -tiny(), type(wide()), type(uwide())])
}
