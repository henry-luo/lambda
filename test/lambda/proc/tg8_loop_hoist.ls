// D8.3.4/DF16: `value` is immutable across the loop. The first call admits an
// int raw key; the loop then uses one entry guard for its int fast sibling and
// the unchanged `_b` sibling for the string case.
fn choose_type(value: any as T) int => 1;
let seed = choose_type(0);

pn count_selected(value: any) int {
    var total: int = 0
    var i: int = 0
    while (i < 4) {
        total = total + choose_type(value)
        i = i + 1
    }
    return total + seed - 1
}

pn main() {
    print([count_selected(7), count_selected("fallback")])
}
