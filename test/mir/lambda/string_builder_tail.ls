// The last recursive argument may own a buffer; ordinary reads still snapshot it.
fn tail_builder(i: int, count: int, acc: string) string {
    if (i >= count) acc else tail_builder(i + 1, count, acc ++ "x")
}

fn tail_snapshots(count: int, previous: array, acc: string) array {
    if (count <= 0) previous ++ [acc]
    else tail_snapshots(count - 1, previous ++ [acc], acc ++ "x")
}

// A non-final accumulator must preserve the later argument's old-value read.
fn nonfinal_builder(count: int, acc: string, previous: string) string {
    if (count <= 0) previous ++ ":" ++ acc
    else nonfinal_builder(count - 1, acc ++ "x", acc)
}

type TextResult = {value: string, column: int}

fn field_tail(count: int, acc: string) TextResult {
    if (count <= 0) {value: acc, column: 0}
    else {
        let piece = make_piece(count)
        field_tail(count - 1, acc ++ piece.value)
    }
}

fn make_piece(column: int) TextResult => {value: "xy", column: column}

fn wrap(value: string) string => "[" ++ value ++ "]"

fn checked_piece(count: int) string^ {
    if (count == 1) raise error("stopped") else "x"
}

fn error_tail(count: int, acc: string) string^ {
    if (count <= 0) acc else error_tail(count - 1, acc ++ checked_piece(count)^)^
}

fn nested_call(count: int, acc: string) string {
    if (count <= 0) acc
    else if (count == 1) {
        let piece = "x"
        wrap(nested_call(count - 1, acc ++ piece))
    } else {
        let piece = "x"
        nested_call(count - 1, acc ++ piece)
    }
}

pn main() {
    let seed = "s"
    print(len(tail_builder(0, 128, seed))); print("\n")
    print(seed); print("\n")
    print(tail_snapshots(3, [], "")); print("\n")
    print(nonfinal_builder(3, "", "")); print("\n")
    print(field_tail(3, "").value); print("\n")
    print(nested_call(2, "")); print("\n")
    let failed = error_tail(3, "") ^ { ^.message }
    print(failed); print("\n")
}
