// test for TOML parsing and formatting
// this script tests TOML input parsing and various output formats

let toml_data = input('./test/input/test.toml', 'toml')

"TOML parsing result:"
toml_data

"formatting TOML as JSON:"
format(toml_data, 'json')

"formatting TOML as XML:"
format(toml_data, 'xml')

"formatting TOML as HTML:"
format(toml_data, 'html')

"formatting TOML as Markdown:"
format(toml_data, 'markdown')

"formatting TOML as YAML:"
format(toml_data, 'yaml')

"formatting TOML as TOML:"
format(toml_data, 'toml')

"test completed."
