// Isolated test for RST (reStructuredText) parsing and formatting
// This script focuses only on RST to help debug the parsing issues

let rst = input('./test/input/test.rst', 'rst')

"RST parsing result:"
rst

"Attempting to format RST as JSON:"
format(rst, 'json')

"Test completed."
