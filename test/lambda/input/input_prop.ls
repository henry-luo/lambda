// test for Properties parsing and formatting
// this script tests Properties input parsing and various output formats

let prop_data = input('./test/input/test.properties', 'properties')

"Properties parsing result:"
prop_data

"formatting Properties as JSON:"
format(prop_data, 'json')

"formatting Properties as XML:"
format(prop_data, 'xml')

"formatting Properties as HTML:"
format(prop_data, 'html')

"formatting Properties as Markdown:"
format(prop_data, 'markdown')

"formatting Properties as YAML:"
format(prop_data, 'yaml')

"formatting Properties as TOML:"
format(prop_data, 'toml')

"formatting Properties as INI:"
format(prop_data, 'ini')

"formatting Properties as Properties:"
format(prop_data, 'properties')

"test completed."
