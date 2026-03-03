// Test: Import Module System
// Layer: 4 | Category: integration | Covers: module pattern, type sharing, encapsulation

// ===== Simulate module pattern with maps/functions =====

// "Math module"
let MathModule = {
    PI: 3.14159265358979,
    E: 2.71828182845905,
    TAU: 3.14159265358979 * 2,

    circle_area: fn(r: float) => 3.14159265358979 * r * r,
    circle_circumference: fn(r: float) => 2.0 * 3.14159265358979 * r,
    degrees_to_radians: fn(deg: float) => deg * 3.14159265358979 / 180.0,
    radians_to_degrees: fn(rad: float) => rad * 180.0 / 3.14159265358979
}

MathModule.PI
MathModule.circle_area(5.0)
MathModule.circle_circumference(5.0)
MathModule.degrees_to_radians(180.0)
MathModule.radians_to_degrees(MathModule.PI)

// "String utils module"
let StringUtils = {
    capitalize: fn(s: string) => upper(s | slice(0, 1)) & (s | slice(1)),
    repeat_str: fn(s: string, n: int) {
        for (i in 1 to n) s
        | join("")
    },
    is_blank: fn(s: string) => s == "" or s == null,
    word_count: fn(s: string) => len(s | split(" "))
}
StringUtils.capitalize("hello")
StringUtils.repeat_str("ab", 3)
StringUtils.is_blank("")
StringUtils.is_blank("hello")
StringUtils.word_count("the quick brown fox")

// "Collection utils module"
let CollectionUtils = {
    chunk: fn(arr, size: int) {
        for (i in 0 to len(arr) - 1 where i % size == 0)
            arr | slice(i, min(i + size, len(arr)))
    },
    zip: fn(a, b) {
        for (i in 0 to min(len(a), len(b)) - 1) [a[i], b[i]]
    },
    flatten: fn(nested) {
        for (sub in nested, item in sub) item
    }
}

CollectionUtils.chunk([1, 2, 3, 4, 5, 6], 2)
CollectionUtils.zip(["a", "b", "c"], [1, 2, 3])
CollectionUtils.flatten([[1, 2], [3, 4], [5, 6]])

// ===== Type definitions shared across "modules" =====
type Result {
    value: int
    status: string
    fn is_ok() => ~.status == "ok"
}

fn compute_result(x: int) => {Result value: x * 2, status: "ok"}
fn error_result(msg: string) => {Result value: 0, status: msg}

let r1 = compute_result(21)
r1.value
r1.is_ok()

let r2 = error_result("failed")
r2.value
r2.is_ok()
r2.status
