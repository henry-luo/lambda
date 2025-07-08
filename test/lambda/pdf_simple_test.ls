// Simple test for basic PDF parsing
let simple_pdf = input('./test/input/test.pdf', 'pdf')
simple_pdf

"\n=== Simple PDF Analysis ===\n"
format(simple_pdf, 'json')
