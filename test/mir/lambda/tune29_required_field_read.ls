// T29-2 (D3.2.6): a required field of a verified record is always present, so
// its packed slot is read with no empty-slot arm, and a member read through it
// needs no receiver null test. Optional fields and records that may be null
// keep both arms; a record passed as a value still reads its fields directly.
type Item = {k: int}
type Box = {items: Item?[], main: Item, spare: Item?, n: int}

pn read_required(b: Box) int {
    return b.main.k
}

pn read_optional(b: Box) any {
    return b.spare
}

pn read_nullable_receiver(b: Box?) any {
    return b.main
}

pn count_items(b: Box) int {
    return len(b.items)
}

pn main() {
    let full: Box = {items: [{k: 3}, null], main: {k: 7}, spare: {k: 9}, n: 2}
    let bare: Box = {items: [], main: {k: 1}, spare: null, n: 0}
    print(read_required(full)); print(" ")
    print(read_optional(full)); print(" ")
    print(read_optional(bare)); print(" ")
    print(read_nullable_receiver(full)); print(" ")
    print(read_nullable_receiver(null)); print(" ")
    print(count_items(full)); print(" ")
    print(count_items(bare)); print("\n")
}
