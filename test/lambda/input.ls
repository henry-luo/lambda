let text = input('./test/input/test.txt', 'text')
text
let json = input('./test/input/test.json', 'json')
json
let csv = input('./test/input/test.csv', 'csv')
csv

let rtf = input('./test/input/test.rtf', 'rtf')
rtf
let pdf = input('./test/input/test.pdf', 'pdf')
pdf

"\nFormat JSON:\n"
format(json, 'json')

"\nFormat RTF:\n"
format(rtf, 'json')

"\nFormat PDF:\n"
format(pdf, 'json')

"\nFormat RST:\n"
format(rst, 'json')

"\nFormat XML:\n"
format(json, 'xml')