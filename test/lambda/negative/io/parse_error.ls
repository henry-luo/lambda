// @expect-error: E403
// @description: JSON parse error - invalid syntax

let json_str = "{ invalid json }"
let data = parse(json_str, "json")
