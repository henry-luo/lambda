// D4.1.4/D7.3.1: dead single-site selections retain their written admission.
pub fn opaque(value: any) any => value
fn word_at(words: string[] as W, index: int) string =>
    if (index < len(words)) words[index] else ""
fn observed(words: string[] as W) type => W
fn captured(words: string[] as W) any {
    fn check() type => W
    return check
}
fn repeated(first: string[] as W, second: W) type => W
pn main() {
    print([word_at(["alpha", "beta"], 1), word_at(["alpha"], 3)]); print("\n")
    print(type(word_at(opaque(7), 0)) == error); print("\n")
    let selected = observed(["alpha"])
    print(type(selected) == type); print("\n")
    let check = captured(["alpha"])
    print(check() == selected); print("\n")
    var words: string[] = ["old", "tail"]
    print(repeated(words, words) == observed(words)); print("\n")
}
