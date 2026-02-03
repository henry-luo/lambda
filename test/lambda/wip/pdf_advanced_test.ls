// Test advanced PDF parsing capabilities
let simple_pdf = input('./test/input/simple_test.pdf', 'pdf')
'simple_pdf:'
simple_pdf

let advanced_pdf = input('./test/input/advanced_test.pdf', 'pdf')
'advanced_pdf:'
advanced_pdf

let raw_commands_pdf = input('./test/input/raw_commands_test.pdf', 'pdf')
'raw_commands_pdf:'
raw_commands_pdf

"\n=== Simple PDF Analysis ===\n"
format(simple_pdf, 'json')

"\n=== Advanced PDF Analysis ===\n"
format(advanced_pdf, 'json')

"\n=== Raw Commands PDF Analysis ===\n"
format(raw_commands_pdf, 'json')
