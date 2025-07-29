// Simple CSS function test
"=== CSS Function Parser Test ==="

let stylesheet = input('./test/input/css_functions_sample.css', 'css')

"Stylesheet:"
type(stylesheet)

"First rule:"
let first_rule = stylesheet.rules[0]
first_rule

"✅ CSS function parser test completed!"
