// D8.3.4/DF16: a dynamic loop call selects each bounded raw key once at entry,
// then uses its raw loop sibling; a non-key still uses the `_b` sibling.
fn choose_type(value: any as T) int => 1;
let int_seed = choose_type(0);
let text_seed = choose_type("seed");

pn count_selected(value: any) int {
    var total: int = 0
    var i: int = 0
    while (i < 4) {
        total = total + choose_type(value)
        i = i + 1
    }
    return total + int_seed - 1
}

pn main() {
    print([count_selected(7), count_selected("raw"), count_selected(true)])
}
