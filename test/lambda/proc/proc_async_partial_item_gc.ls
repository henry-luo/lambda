pn delayed_u64() {
    sleep(1)^
    return 18446744073709551614u64
}

pn main() {
    let handle = start delayed_u64()
    // The array and its first wide-scalar payload exist before wait suspends
    // this expression; both must remain owned and traced until construction resumes.
    let values = [9223372036854775807i64, wait(handle)^, 5e-324]
    print(values)
}
