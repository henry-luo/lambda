// S2.5.1-S2.5.5: sequence collection preserves values and item-position void.
fn collected() {
    1
    null
    ""
    2
}

fn working_state() {
    var value = 0
    value = value + 1
    value
    value = value + 1
    value
}

fn early_return() {
    1
    return 99
    2
}

fn nullable_branch(flag: bool) {
    1
    if (flag) 2
    3
}

pn procedural_result() {
    1
    let ignored = 2
    null
}

pn result_before_tail_statements() {
    var value = 0
    9
    value = value + 1
    while (value < 3) { value = value + 1 }
}

pn main() {
    print(collected()); print("\n")
    print(working_state()); print("\n")
    print(early_return()); print("\n")
    print(nullable_branch(true)); print("\n")
    print(nullable_branch(false)); print("\n")
    print(procedural_result()); print("\n")
    print(result_before_tail_statements()); print("\n")
    print((1, (2, 3), null, "", "a", "b")); print("\n")
    print((let value = 3, value + 1)); print("\n")
    print([(), 1]); print("\n")
    let empty = ()
    print([empty, 1]); print("\n")
    print([for (value in []) value, 1]); print("\n")
    let empty_query = for (value in []) value
    print([empty_query, 1]); print("\n")
    print([if (true) () else (7, 8), 2]); print("\n")
    print([*[], 1]); print("\n")
    let source = [1, 2]
    print((*source, 3)); print("\n")
    print([source, 3]); print("\n")
}
