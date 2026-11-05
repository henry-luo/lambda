// S8.2.4v3/S2.5.5v2: queries collapse their result once and remain values.
// S11.1.2v4: text patterns retain their string or symbol domain.
// S2.4.2v5: computed path steps and their suffixes are key operations.
type Digits = \("\d"+)
type Symbols = \('\d'+)
fn classify(value) => match value {
    case \("\d"+): "string"
    case Symbols: "symbol"
    default: "other"
}
fn path_at(key: any) any => \.row[key].name
pn main() {
    let tree = <root <leaf n: 1> <branch <leaf n: 2>>>
    print([count(tree?<leaf>), count(tree.?<root>), count(tree?<root>)]); print("\n")
    print([count(42?int), count(42.?int), 42.?int]); print("\n")
    print([tree?<missing>, 9]); print("\n")
    print([*tree?<missing>, 9]); print("\n")
    print([count({a: 1, b: [2, "x"], c: 3.5}?float), count(tree?int)]); print("\n")
    print(["123" is Digits, '123' is Digits, '123' is Symbols, "123" is Symbols]); print("\n")
    print([classify("12"), classify('12'), classify("x"), 5 is \("\d"+)]); print("\n")
    print(["34" is \("\d"+), '34' is \('\d'+)]); print("\n")
    print([path_at(2), path_at('a'), path_at("b"), path_at(null)]); print("\n")
    print([for (key in [1, 2]) \.row[key].name]); print("\n")
}
