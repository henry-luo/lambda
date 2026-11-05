// D4.3.1–D4.3.3: readonly observations preserve element kinds and open writes.
fn total(values) {
    var result = 0.0
    var index = 0
    while (index < len(values)) {
        result = result + values[index]
        index = index + 1
    }
    return result
}
fn first(values) { return values[0] }
fn open_first(values) { return values[0] }
fn escaping(values) { return values[0] }
fn maybe_values(fail: bool) float[]^ {
    if (fail) { raise "failed" }
    return [2.5, 3.5]
}
fn closed_reader(values) { return values[0] }
pn replace(var values) { values[0] = "changed" }
pn main() {
    var one = [1.25]
    var three = [1.25, 2.5, 3.75]
    print([total(one) == 1.25, total(three) == 7.5]); print("\n")
    print([type(first([3, 4])) == int, type(first([2.5, 3.5])) == float]); print("\n")
    var changing = [1, 2]
    let before = open_first(changing)
    replace(changing)
    print([before == 1, open_first(changing) == "changed"]); print("\n")
    let observed = escaping([2.5])
    let callback = escaping
    print([observed == 2.5, callback(["text"]) == "text"]); print("\n")
    let accepted = closed_reader(maybe_values(false))
    let rejected = closed_reader(maybe_values(true))
    print([accepted == 2.5, rejected is error]); print("\n")
}
