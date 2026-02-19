// Test: typed string parameters on user-defined functions
// Issue #16: `: string` annotation makes C param `String*`, but callers
// may pass `Item` (e.g., untyped args), causing C2MIR type mismatch.

// Function with typed string parameter
fn greet(name: string) {
  "Hello, " ++ name
}

// 1. Direct string literal
"1. literal"
greet("world")

// 2. Passing through untyped function parameter
fn relay(x) {
  greet(x)
}
"2. untyped relay"
relay("Lambda")

// 3. str_join result (sys func returning Item despite STRING type)
"3. str_join"
let parts = ["a", "b", "c"]
greet(str_join(parts, "-"))

// 4. Multiple typed params
fn pair(a: string, b: string) {
  a ++ "+" ++ b
}
"4. multi-param"
pair("x", "y")

// 5. Mixed typed and untyped params
fn tag(label: string, value) {
  label ++ "=" ++ string(value)
}
"5. mixed params"
tag("count", 42)

// 6. Typed bool param
fn toggle(flag: bool) {
  if (flag) "on" else "off"
}
"6. bool param"
toggle(true)
toggle(false)

// 7. Untyped arg to bool param
fn relay_bool(x) {
  toggle(x)
}
"7. bool relay"
relay_bool(true)
