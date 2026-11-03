// recursive plain parameters retain activation snapshots and precise roots (S9.1.2, D5.3).
pn local_sum(values: int[], depth: int) int^ {
    values[0] = values[0] + 1
    if (depth == 0) { return values[0] }
    let child: int = local_sum(values, depth - 1)^
    let garbage = fill(64, depth)^
    return child + values[0]
}
pn sibling(values: int[], var other: int[], depth: int) int^ {
    values[0] = values[0] + 1
    other[0] = other[0] + 10
    if (depth > 0) { sibling(values, other, depth - 1)^ }
    return values[0]
}
pn change_argument(var values: int[]) int {
    values[0] = 99
    return 2
}
pn call_sibling(var values: int[]) int^ {
    return sibling(values, values, 1)^
}
pn retained(values: int[], depth: int) int[]^ {
    values[0] = values[0] + 1
    if (depth == 0) { return values }
    return retained(values, depth - 1)
}
pn keyed_escape(values: int[], depth: int, key: any) any {
    let window = values[key]
    values[0] = values[0] + 1
    if (depth == 0) { return window }
    return keyed_escape(values, depth - 1, key)
}
pn main() {
    var values: int[] = [3, 7]
    print([local_sum(values, 2), values[0]]); print("\n")
    let kept = values
    print([sibling(values, values, 2), values[0], kept[0]]); print("\n")
    values[0] = 3
    print([local_sum(values, change_argument(values)), values[0]]); print("\n")
    values[0] = 3
    print([retained(values, 2)[0], values[0]]); print("\n")
    print([keyed_escape(values, 1, [0, 1])[0], values[0]]); print("\n")
    var large: int[] = fill(128, 3)^
    print([local_sum(large, 2), large[0], len(large)]); print("\n")
    var alias: int[] = [3]
    print([call_sibling(alias), alias[0]]); print("\n")
    var empty: int[] = []
    print(local_sum(empty, 1) is error); print("\n")
    let missing: int[] = null
    print(local_sum(missing, 1) is error); print("\n")
}
