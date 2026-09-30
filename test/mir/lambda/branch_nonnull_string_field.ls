// S7.1.1v3/D3.2.6: a stable receiver narrowed by a null test can read a
// required string slot directly; an unguarded or rebound receiver still chains null.
type Reading = {text: string, count: int}

fn guarded(x: Reading?) string => if (x == null) "" else x.text
fn unguarded(x: Reading?) any => x.text

pn rebound(x: Reading?) any {
    if (x != null) {
        x = null
        return x.text
    }
    return null
}

pn main() {
    let present: Reading = {text: "hello", count: 1}
    print(guarded(present)); print("|")
    print(guarded(null)); print("|")
    print(unguarded(null)); print("|")
    print(rebound(present)); print("\n")
}
