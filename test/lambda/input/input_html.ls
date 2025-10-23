// Test for HTML parsing and formatting
// This script tests HTML input parsing and various output formats

let html = input('./test/input/test.html', 'html')

'HTML parsing result:'
html

'Formatting HTML as XML:'
format(html, 'xml')

'Formatting HTML as HTML:'
format(html, 'html')

'Test completed.'
