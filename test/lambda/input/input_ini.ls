// test for INI parsing and formatting
// this script tests INI input parsing and various output formats

let ini_data = input('./test/input/test.ini', 'ini')

"INI parsing result:"
ini_data

"formatting INI as JSON:"
format(ini_data, 'json')

"formatting INI as XML:"
format(ini_data, 'xml')

"formatting INI as HTML:"
format(ini_data, 'html')

"formatting INI as Markdown:"
format(ini_data, 'markdown')

"formatting INI as YAML:"
format(ini_data, 'yaml')

"formatting INI as TOML:"
format(ini_data, 'toml')

"formatting INI as INI:"
format(ini_data, 'ini')

"test completed."
