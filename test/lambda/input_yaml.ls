// test for YAML parsing and formatting
// this script tests YAML input parsing and various output formats

let yaml_data = input('./test/input/test.yaml', 'yaml')

"YAML parsing result:"
yaml_data

"formatting YAML as JSON:"
format(yaml_data, 'json')

"formatting YAML as XML:"
format(yaml_data, 'xml')

"formatting YAML as HTML:"
format(yaml_data, 'html')

"formatting YAML as Markdown:"
format(yaml_data, 'markdown')

"formatting YAML as YAML:"
format(yaml_data, 'yaml')

"test completed."
