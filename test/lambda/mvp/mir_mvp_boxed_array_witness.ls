// D2.3.4/S7.1.1v3: a physical witness supplies storage, not an element domain.
fn peek(values: any, key: int) any { return [len(values), values[key]] }
pn mutate(var values: any) any {
    let first = values[0]
    let before = len(values)
    push(values, 3)^
    let grown = len(values)
    values = "done"
    return [first, before, grown, len(values)]
}
// S9.1.4v2: a confined pn retains the parent fn's precisely rooted owner home.
fn shared_owner(seed: any) any {
    var values = seed
    pn read_owner() { return values }
    let before = [len(values), values[0]]
    values = "done"
    return [before, len(values), values[0], read_owner == read_owner]
}
pn main() {
    var values = []
    push(values, 1)^
    push(values, "two")^
    let saved = values
    print(peek(values, 1)); print("\n")
    print(peek(fill(3, 0), 1)); print("\n")
    print(peek("éλ", 1)); print("\n")
    print(peek([[1, 2], [3, 4]], 0)); print("\n")
    print(mutate(values)); print("\n")
    print([saved, values]); print("\n")
    print(shared_owner(saved)); print("\n")
}
