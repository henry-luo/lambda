// closed tables refine reads; inferred containers still admit other contents.
fn choose(start: int) int^ {
    var table = [2, 0, 1]
    var values: int[] = [10, 20, 30]
    var sum: int = 0
    var i: int = start
    while (i < 3) {
        if (values[table[i]] > 0) { sum = sum + values[table[i]] }
        i = i + 1
    }
    return sum
}

fn maybe_int(yes: bool) int? { return if (yes) 7 else null }
fn maybe_float(yes: bool) float? { return if (yes) 1.5 else null }
fn maybe_bool(yes: bool) bool? { return if (yes) true else null }
fn maybe_text(yes: bool) string? { return if (yes) "yes" else null }

pn main() {
    print(choose(0)); print(" "); print(choose(2)); print(" "); print(choose(3)); print("\n")
    var mixed = [1, "two"]
    let image = mixed
    mixed[0] = 3.5
    print(mixed[0]); print(" "); print(image[0]); print("\n")
    var record = {count: 1}
    let before = record
    record.count = 2
    print(record.count); print(" "); print(before.count); print("\n")
    var words: string[] = ["A", "B"]
    print("x" ++ words[4] ++ "y"); print(" "); print(words[-1]); print("\n")
    print(maybe_int(true)); print(" "); print(maybe_int(false)); print("\n")
    print(maybe_float(true)); print(" "); print(maybe_float(false)); print("\n")
    print(maybe_bool(true)); print(" "); print(maybe_bool(false)); print("\n")
    print(maybe_text(true)); print(" "); print(maybe_text(false)); print("\n")
    var data: int[] = [1, 2, 3]
    let observed: int[] = data
    for i of data { data[i] = data[i] + 10 }
    print(observed[0]); print(" "); print(data[0]); print("\n")
}
