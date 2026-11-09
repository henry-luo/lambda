// D4.3.1/S9.1.3v2: private array domains preserve snapshots and open writes.
let COUNT = 2
pn descend(flags, depth) {
    if (depth == 0) { return if (flags[0]) 1 else 0 }
    flags[0] = true
    return descend(flags, depth - 1)
}
pn escaped(flags) { flags[0] = true; return flags }
pn retag(value: any) any {
    var values = fill(2, 0)
    let saved = values
    values[0] = value
    return [saved[0], values[0]]
}
pn replace_owner(var values) { values = ["changed"] }
pn clear_slots() {
    var flags = fill(COUNT, true)
    var i = 0
    while (i < COUNT) {
        flags[i] = false
        i = i + 1
    }
    return flags
}
pn main() {
    var flags = fill(COUNT, false)
    print([descend(flags, 3), flags[0]]); print("\n")
    print([escaped(flags), flags]); print("\n")
    print(retag("word")); print("\n")
    print(retag(null)); print("\n")
    let failed = retag(error("kept"))
    print(failed[1] is error); print("\n")
    var values = fill(2, 0)
    replace_owner(values)^
    print(values); print("\n")
    var rejected = fill(-1, 0)
    print(rejected is error); print("\n")
    print(clear_slots()); print("\n")
}
