// Test for Org Mode parsing and formatting
// This script tests Org input parsing and various output formats

let org_data = input('./temp/test.org', 'org')

"Org Mode parsing result:"
org_data

"formatting Org as JSON:"
format(org_data, 'json')

"formatting Org as XML:"
format(org_data, 'xml')

"formatting Org as HTML:"
format(org_data, 'html')

"formatting Org as Markdown:"
format(org_data, 'markdown')

"formatting Org as YAML:"
format(org_data, 'yaml')

"test completed."
