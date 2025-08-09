// Test for CSS parsing and formatting
// This script tests CSS input parsing and various output formats

let css = input('./test/input/simple.css', 'css')

"CSS parsing result:"
css

"Formatting CSS as JSON:"
format(css, 'json')

"Formatting CSS as CSS:"
format(css, 'css')

"Test completed."
