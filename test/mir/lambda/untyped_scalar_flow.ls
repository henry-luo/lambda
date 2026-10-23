// D2.4.1-D2.4.3, S7.8.1: inferred joins preserve carriers and boxed fallbacks.
import numbers: lambda.chart.numbers

pn flow_magnitude(n) {
    var value = if (n < 0) -n else n
    while (value >= 10) { value = value div 10 }
    return value
}
pn flow_float(n, left, right) {
    var value = if (n < 0) left else right
    value = value + 0.5
    return value
}
pn flow_prefix(n) {
    var text = if (n < 0) "-" else ""
    var i = 0
    while (i < 3) {
        text = text ++ chr(65 + i)
        i = i + 1
    }
    return text
}
pn flow_snapshot(n) {
    var text = if (n < 0) "-" else ""
    let before = text
    text = text ++ "A"
    return before ++ "|" ++ text
}
pn flow_widened(n) {
    var value = if (n < 0) -n else n
    if (n == 0) { value = "zero" }
    return value
}
pn flow_replace(var value) { value = "changed" }
pn flow_borrowed(n) {
    var value = if (n < 0) -n else n
    flow_replace(value)
    return value
}
fn flow_error_condition(condition) => if (condition) 1 else 2
fn flow_mixed(condition) => if (condition) 1 else "other"
fn flow_optional(condition) => if (condition) 7
fn flow_tail_join(text) {
    let parts = split(text, ":")
    "x" ++ (if (len(parts) > 1) "y" ++ parts[1] else "")
}
pn flow_compare(rows, delta) {
    let left = rows[0]
    let right = rows[1]
    // a numeric success witness does not discharge the boxed error arm (S3.4).
    return [left - right == delta or left == right,
        left - right == delta and left != right,
        not (left - right == delta), (left - right == delta) == true]
}
pn flow_compare_loop(rows) {
    var row = 0
    while (row < 8) {
        let left = rows[row]
        let right = rows[row + 1]
        if (left - right == row or false) { return 0 }
        row = row + 1
    }
    return 1
}
pn main() {
    print([flow_magnitude(-23456), flow_magnitude(12345), flow_magnitude(9007199254740991)])
    print("\n")
    print([flow_float(-1, 1.25, 2.25), flow_float(1, 1.25, 2.25)])
    print("\n")
    print([flow_prefix(-1), flow_prefix(1), flow_snapshot(-1), flow_snapshot(1)])
    print("\n")
    print([flow_widened(-2), flow_widened(0), flow_borrowed(-1)])
    print("\n")
    print([type(flow_error_condition(error("stop"))) == error,
        flow_mixed(true) == 1, flow_mixed(false) == "other",
        flow_optional(false) == null])
    print("\n")
    var ascii_ok = true
    var code = 0
    while (code < 128) {
        let text = chr(code)
        ascii_ok = ascii_ok and len(text) == 1 and ord(text) == code
        code = code + 1
    }
    print([ascii_ok, chr(233) == "é", chr(128512) == "😀",
        chr(-1) == "", chr(55296) == "", chr(1114112) == "", chr(null) == ""])
    print("\n")
    print([flow_tail_join("a:b"), flow_tail_join("a")])
    print("\n")
    print([flow_compare([4, 1], 3), flow_compare([4, 1], 2),
        flow_compare([1, 1], 2), flow_compare([null, 1], 3)])
    print("\n")
    let comparison_errors = flow_compare([error("stop"), 1], 3)
    print([for (value in comparison_errors) type(value) == error])
    print("\n")
    print(flow_compare_loop([0, 4, 7, 5, 2, 6, 1, 3]))
    print("\n")
    print([numbers.concise_number("1.230e5"), numbers.concise_number("2.000")])
    print("\n")
    let markup = format(<span <b chr(65)> <b chr(43)> <b chr(49)>>, 'xml')
    print([contains(markup, ">A<"), contains(markup, ">+<"), contains(markup, ">1<")])
    print("\n")
}
