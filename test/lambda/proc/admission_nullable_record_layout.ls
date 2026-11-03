// S11.4.6v3/D3.2.4v3: nullable record bindings reify their normal map layout.
type Link = {value: int, next: Link?}

pn main() {
    let tail: Link? = {value: 2, next: null}
    let head: Link? = {leading: "extra", value: 1, next: tail}
    var other: Link? = {leading: 99, value: 3, next: head}
    let absent: Link? = null
    print([head.value, head.next.value, other.value, other.next.value, absent])
    print("\n")
}
