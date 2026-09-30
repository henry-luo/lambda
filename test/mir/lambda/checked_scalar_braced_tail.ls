// D8.2.6/S9.1.2: declarations before a checked scalar tail do not require
// another native frame; an ordinary non-tail recursive operand still does.
fn checked_tail(parts: string[], index: int, count: int, acc: int) int {
    if (index >= count) acc
    else {
        let marker = string(index)
        let part = parts[index]
        if (part == null) acc
        else checked_tail(parts, index + 1, count,
            acc + len(part) + len(marker) - 1)
    }
}

fn non_tail(n: int) int {
    if (n <= 0) 0
    else {
        let step = n
        step + non_tail(n - 1)
    }
}

fn changing_owner(parts: string[], n: int) int {
    if (n <= 0) len(parts)
    else {
        let next = parts ++ ["z"]
        changing_owner(next, n - 1)
    }
}

pn main() {
    let parts: string[] = ["ab", "c", ""]
    print([checked_tail(parts, 0, 3, 0),
           checked_tail(parts, 0, 4, 0),
           checked_tail(parts, 0, 0, 0), non_tail(4),
           changing_owner(["a"], 2)])
    print("\n")
}
