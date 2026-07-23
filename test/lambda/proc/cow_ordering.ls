// COW ordering: evaluate a RHS borrow before the enclosing write, then
// resolve the store target against the replacement owner.

pn replace_first(var values: any[]) {
    values[0] = 7
    return 9
}

pn move_first(var values: any[]) {
    values[0] = values[1]
    return 0
}

pn main() {
    var original: any[] = [1]
    var changed: any[] = original
    changed[0] = replace_first(changed)
    print(original[0] ++ " " ++ changed[0] ++ "\n")

    var source: any[] = [[1], [2]]
    var target: any[] = source
    var index = 0
    target[index] = move_first(target)
    print(source[0][0] ++ " " ++ target[0] ++ " " ++ target[1][0] ++ "\n")
}
