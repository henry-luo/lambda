// S11.2.1/S10.1.3: ordered patterns and nested current-value contexts.
type Record = {a: int}
type AnyError = error
fn classify(value: any) any {
    match value {
        case 0: "zero"
        case 80 to 89 | 90 to 100: "high"
        case "hello": "greeting"
        case Record: "record"
        case error: "failed"
        default: "other"
    }
}
fn dynamic_pattern(value: any, pattern: any) any {
    match value { case pattern: "hit" default: "miss" }
}
fn nested_subject(value: any) any {
    match value {
        case int: [~, match 9 { case 9: ~ }, ~, ~key]
        default: null
    }
}
pn main() {
    print([classify(0), classify(85), classify(95), classify("hello"), classify({a: 1})]); print("\n")
    print([classify(7), classify(shl(1, -1))]); print("\n")
    print([dynamic_pattern(3, int), dynamic_pattern(3, string), dynamic_pattern(3, 1 to 5),
           dynamic_pattern(3, 3), dynamic_pattern(3, 4)]); print("\n")
    print(nested_subject(3)); print("\n")
    let recovered = shl(1, -1) ^AnyError { 7 }
    let untouched = 8 ^AnyError { 99 }
    let enclosing = match 12 { case int: null or^ { ~ } }
    print([recovered, untouched, enclosing, false or^ { ^ == false }]); print("\n")
}
