import rtf_package: lambda.doc.rtf
let rtf = rtf_package.parse_file('./test/input/test.rtf')^
rtf

"\nFormat RTF:\n"
format(rtf, 'json')

"All tests completed!"
