// D4.3.1/D6.3/S7.1.1v3: physical keys keep checked misses and COW snapshots.
pub pn integer_store(key: any, member: any) any {
    var values = fill(3, 0)
    let saved = values
    values[key] = member
    return [saved, values]
}
pub pn flag_store(key: any, member: bool) any^ {
    var values: bool[] = fill(2, false)^
    let saved = values
    values[key] = member
    return [saved, values]
}
pub pn float_store(key: any, member: float?) any {
    var values = fill(2, 1.5)
    let saved = values
    values[key] = member
    return [saved, values]
}
fn ordinal(text: string, key: any) any => ord(text[key])
pub pn opaque_store(var values: any, key: any, member: any) any {
    let saved = values
    values[key] = member
    values[0] = member
    return [saved, values]
}
pn main() {
    print(integer_store(1, 7)); print("\n")
    print(integer_store(1, "wide")); print("\n")
    print(flag_store(1, true)^); print("\n")
    print(float_store(0, -2.5)); print("\n")
    print(float_store(1, null)); print("\n")
    print([ordinal("Az", 0), ordinal("Az", 1), ordinal("Az", -1), ordinal("Az", 2)]); print("\n")
    print([ordinal("é水", 0), ordinal("é水", 1), ordinal(chr(0), 0)]); print("\n")
    print([ordinal("Az", null), ordinal("Az", "key"), ordinal("Az", true), ordinal("Az", 0.5)]); print("\n")
    var floats = fill(2, 1.5)^
    print(opaque_store(floats, 1, -2.5)); print("\n")
    var flags = fill(2, true)^
    print(opaque_store(flags, 1, false)); print("\n")
    var absent = fill(2, 1.5)^
    print(opaque_store(absent, 1, null)); print("\n")
}
