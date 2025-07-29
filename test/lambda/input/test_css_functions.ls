// Simple CSS function test
"=== CSS Function Parser Test ==="

// Create a simple CSS file with functions for testing
let css_with_functions = input('./test/input/css_functions_sample.css', 'css')

"Stylesheet:"
css_with_functions

"First rule:"
let first_rule = css_with_functions.rules[0]
first_rule

"Background property (should be linear-gradient element):"
first_rule.background

"Transform property (should be scale element):"
first_rule.transform

"✅ CSS function parser test completed!"
