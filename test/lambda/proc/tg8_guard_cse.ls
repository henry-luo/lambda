// D8.3.4/DF16: the second straight-line call reuses the first call's exact
// variant choice; both calls still execute and the boxed miss stays `_b`.
fn choose_type(value: any as T) int => len(string(name(T)));
let int_seed = choose_type(0);
let text_seed = choose_type("seed");

pn choose_twice(value: any) any {
    let stable: any = value
    return [choose_type(stable), choose_type(stable)]
}

pn choose_after_rebind() any {
    var value: any = 7
    let first = choose_type(value)
    value = "after"
    let second = choose_type(value)
    return [first, second]
}

pn main() {
    print([choose_twice(7), choose_twice("raw"), choose_twice(true),
        choose_after_rebind()])
}
