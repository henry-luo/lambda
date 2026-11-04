// private domains must not constrain inferred writes (S12.2.1v3).
// require_flag also records a pre-existing Full MIR discrepancy: an absent
// selected bool must fail its non-null return boundary (S3.4, S11.4.1v5).
type Cell = {value: int, next: Cell?}
fn cell(value: int) Cell { let result = {value: value, next: null}; return result }
fn transient_cell(value: int) Cell { var result = {value: value, next: null}; return result }
fn retained_cell(value: int) Cell {
    var result: Cell = {value: value, next: null}
    let observed = result
    result.value = result.value + 1
    return observed
}
fn checked_cell(value) Cell { let result = {value: value, next: null}; return result }
fn nullable_text(present: bool) string? { return if (present) "text" else null }
fn text_length(value: string) int { return len(value) }

fn shifted(a: float[], b: float[]) float[] {
    return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
}

pn ready(board: bool[], start: int) int {
    var lookup = [2, 0, 1]
    if (board[lookup[start]] and not board[lookup[start + 1]]) { return 1 }
    return 0
}
fn require_flag(flags: bool[], key: int) bool { return flags[key] and true }
pn put_erased(var values: int[], key: int, member: any) any { values[key] = member; return null }

// recursive forwarding keeps caller-owned precise homes alive across GC (D6.3).
pn rewrite(var a: int[], var box: Cell, depth: int) any {
    if (depth == 0) {
        a = fill(3, 40)
        box = {value: 50, next: null}
        return null
    }
    let garbage = fill(8, depth)
    rewrite(a, box, depth - 1)
    a[0] = a[0] + 1
    box.value = box.value + 1
}

pn main() {
    var stable = fill(3, 0)
    let observed = stable
    stable[0] = 7
    print(stable[0]); print(" "); print(observed[0]); print("\n")
    var widened = fill(2, 0)
    widened[0] = 2.5
    print(widened[0]); print("\n")
    var words = [1, 2]
    words[0] = "word"
    print(words[0]); print("\n")
    let rejected = fill(-1, 0)
    print(match rejected { case error: true default: false }); print("\n")

    var cursor = cell(1)
    cursor = {value: "retagged", extra: true}
    print(cursor.value); print("\n")
    var moved: Cell = transient_cell(6)
    let snapshot = moved
    moved.value = 7
    print(snapshot.value); print(" "); print(moved.value); print(" ")
    print(retained_cell(8).value); print("\n")
    let bad = checked_cell("bad")
    print(match bad { case error: true default: false }); print(" ")
    print(checked_cell(9).value); print("\n")
    print(text_length(nullable_text(true))); print(" ")
    let absent = text_length(nullable_text(false))
    print(match absent { case error: true default: false }); print("\n")

    var a: float[] = [1.0, 2.0, 3.0]
    var b: float[] = [4.0, 5.0, 6.0]
    let vector = shifted(a, b)
    print(vector[0]); print(" "); print(vector[1]); print(" "); print(vector[2]); print("\n")
    let short = shifted([1.0], b)
    print(match short { case error: true default: false }); print("\n")
    var flags: bool[] = [false, true, true]
    print(ready(flags, 0)); print(" "); print(ready(flags, 1)); print(" ")
    print(ready(flags, 2)); print(" "); print(ready(flags, 3)); print(" ")
    print(ready(flags, -1)); print("\n")
    print(not flags[9]); print(" "); print(flags[9] and true); print(" ")
    print(flags[9] or false); print("\n")
    print(require_flag(flags, 0)); print(" ")
    let missing_flag = require_flag(flags, 9)
    print(match missing_flag { case error: true default: false }); print("\n")
    var typed: int[] = [1, 2]
    let original = typed
    put_erased(typed, 0, 7)
    let bad_member = put_erased(typed, 1, "bad")
    print(original[0]); print(" "); print(typed[0]); print(" ")
    print(match bad_member { case error: true default: false }); print(" "); print(typed[1]); print("\n")

    var data: int[] = [9]
    var box: Cell = cell(1)
    let old_data = data
    let old_box = box
    rewrite(data, box, 8)
    print(data[0]); print(" "); print(box.value); print(" ")
    print(old_data[0]); print(" "); print(old_box.value); print("\n")
}
