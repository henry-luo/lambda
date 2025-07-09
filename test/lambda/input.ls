let text = input('./test/input/test.txt', 'text')
text
let json = input('./test/input/test.json', 'json')
json
let csv = input('./test/input/test.csv', 'csv')
csv
// let ini = input('./test/input/test.ini', 'ini')
// ini
// let toml = input('./test/input/test.toml', 'toml')
// toml
// let yaml = input('./test/input/test.yaml', 'yaml')
// yaml
// let more_yaml = input('./test/input/more_test.yaml', 'yaml')
// more_yaml
// let xml = input('./test/input/test.xml', 'xml')
// xml
// let more_xml = input('./test/input/more_test.xml', 'xml')
// more_xml
let markdown = input('./test/input/test.md', 'markdown')
markdown
// let html = input('./test/input/test.html', 'html')
// html
// let more_html = input('./test/input/more_test.html', 'html')
// more_html
// let latex = input('./test/input/test.tex', 'latex')
// latex
let rtf = input('./test/input/test.rtf', 'rtf')
rtf
let pdf = input('./test/input/test.pdf', 'pdf')
pdf
let rst = input('./test/input/test.rst', 'rst')
rst

"\nFormat JSON:\n"
format(json, 'json')

"\nFormat Markdown:\n"
format(markdown, 'markdown')

"\nFormat RTF:\n"
format(rtf, 'json')

"\nFormat PDF:\n"
format(pdf, 'json')

"\nFormat RST:\n"
format(rst, 'json')

"\nFormat XML:\n"
format(json, 'xml')

"\nFormat Markdown as XML:\n"
format(markdown, 'xml')