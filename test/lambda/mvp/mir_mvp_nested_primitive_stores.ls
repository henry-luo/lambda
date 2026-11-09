// D6.3/S9.1.2: a direct row store needs an exclusive spine, never a borrowed buffer.
pn put_int(var owner, row: any, column: any, incoming: int) any {
    owner[row][column] = incoming
    return owner
}
pn put_float(var owner, row: int, column: int, incoming: float) any {
    owner[row][column] = incoming
    return owner
}
pn put_bool(var owner, row: int, column: int, incoming: bool) any {
    owner[row][column] = incoming
    return owner
}
pn next_key(var counter: int) int {
    counter = counter + 1
    return 0
}
pn main() {
    var ints = fill(1, null)^
    ints[0] = fill(2, 0)^
    put_int(ints, 0, 1, 7)^
    print(ints); print("\n")
    let saved = ints
    put_int(ints, 0, 0, 9)^
    print([ints, saved]); print("\n")
    let saved_row = ints[0]
    put_int(ints, 0, 1, 11)^
    print([ints, saved_row]); print("\n")
    var floats = fill(1, null)^
    floats[0] = fill(2, 0.5)^
    put_float(floats, 0, 1, -2.5)^
    print(floats); print("\n")
    var flags = fill(1, null)^
    flags[0] = fill(2, true)^
    put_bool(flags, 0, 1, false)^
    print(flags); print("\n")
    var mixed: any = [["old", false]]
    put_int(mixed, 0, 0, 3)^
    print(mixed); print("\n")
    var effects = 0
    put_int(ints, next_key(effects), next_key(effects), 13)^
    print([effects, ints]); print("\n")
    // invalid paths preserve the checked writer's Error and the original rows.
    let negative: any = put_int(ints, 0, -1, 99)
    let poison: any = put_int(ints, 0, 0/0, 99)
    let fractional: any = put_int(ints, 0, 0.5, 99)
    print([negative is error, poison is error, fractional is error, ints]); print("\n")
    var invalid: any = ["0123456789abcdefg"]
    let wrong_owner: any = put_int(invalid, 0, 0, 99)
    print([wrong_owner is error, invalid]); print("\n")
}
