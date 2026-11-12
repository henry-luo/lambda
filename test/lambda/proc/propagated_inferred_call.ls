// LR12-49 / S7.6.3v3, D2.3.4: a discharged untyped call keeps its inferred
// success contract when its Item result enters an inferred native binding.
fn label(v) => string(v) ^ { "fallback" }
fn amount(v) => float(v) ^ { 0.25 }
fn words(v) => split(v, " ") ^ { ["fallback"] }

pn observe(v) {
    let text = label(v)^
    let parsed = amount(v)^
    let parts = words(v)^
    let garbage = fill(128, "keep the call results live")^
    return [text ++ "!", parsed + 1.0, len(parts), parts[0]]
}

fn checked(v: string) => string(v)
pn rejected(var calls, v) {
    let text = checked(v)^
    calls = calls + 1
    return text
}

pn main() {
    print(observe("2")); print("\n")
    print(observe("two words")); print("\n")
    var calls = 0
    let failure = rejected(calls, 7)
    print([failure is error, calls, rejected(calls, "ok"), calls]); print("\n")
}
