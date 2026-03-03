// Test: Match Exhaustiveness
// Layer: 3 | Category: boundary | Covers: many arms, deep nesting, large or-patterns

// ===== Many match arms =====
fn day_name(n: int) => match n {
    case 1: "Monday"
    case 2: "Tuesday"
    case 3: "Wednesday"
    case 4: "Thursday"
    case 5: "Friday"
    case 6: "Saturday"
    case 7: "Sunday"
    default: "Unknown"
}
day_name(1)
day_name(4)
day_name(7)
day_name(0)

// ===== Match with many type arms =====
fn type_name(v) => match v {
    case int: "int"
    case float: "float"
    case string: "string"
    case bool: "bool"
    case null: "null"
    case list: "list"
    case array: "array"
    case map: "map"
    default: "other"
}
type_name(42)
type_name(3.14)
type_name("hi")
type_name(true)
type_name(null)
type_name((1, 2))
type_name([1, 2])
type_name({a: 1})

// ===== Nested match =====
fn classify(a, b) => match a {
    case int: match b {
        case int: "both int"
        case string: "int+string"
        default: "int+other"
    }
    case string: match b {
        case int: "string+int"
        case string: "both string"
        default: "string+other"
    }
    default: "other"
}
classify(1, 2)
classify(1, "a")
classify("a", 1)
classify("a", "b")
classify(true, true)

// ===== Range patterns =====
fn char_class(c: int) => match c {
    case 48 to 57: "digit"
    case 65 to 90: "uppercase"
    case 97 to 122: "lowercase"
    case 32: "space"
    default: "other"
}
char_class(65)
char_class(97)
char_class(48)
char_class(32)
char_class(0)

// ===== Or-pattern with multiple alternatives =====
fn is_vowel_code(c: int) => match c {
    case 65 | 69 | 73 | 79 | 85 | 97 | 101 | 105 | 111 | 117: true
    default: false
}
is_vowel_code(65)
is_vowel_code(66)
is_vowel_code(97)

// ===== Match on boolean =====
fn evaluate(cond: bool) => match cond {
    case true: "yes"
    case false: "no"
}
evaluate(true)
evaluate(false)

// ===== Default only =====
fn always_default(x) => match x {
    default: "always this"
}
always_default(42)
always_default("anything")
always_default(null)

// ===== Single arm + default =====
fn check_zero(n: int) => match n {
    case 0: "zero"
    default: "nonzero"
}
check_zero(0)
check_zero(1)
check_zero(-1)
