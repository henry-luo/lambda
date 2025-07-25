// Test for HTML parsing and formatting
// This script tests HTML input parsing and various output formats

let html = input('./test/input/test.html', 'html'), 
    more_html = input('./test/input/more_test.html', 'html')

"HTML parsing result:"
html

"Formatting HTML as JSON:"
format(html, 'json')

"Formatting HTML as XML:"
format(html, 'xml')

"Formatting HTML as HTML:"
format(html, 'html')

"Formatting HTML as Markdown:"
format(html, 'markdown')

more_html

"Test completed."
