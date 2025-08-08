// Test for Org Mode parsing and formatting
// This script tests Org input parsing and various output formats

let org_data = input('./temp/test.org', 'org')

"Org Mode parsing result:"
org_data

"formatting Org as JSON:"
format(org_data, 'json')

"Testing Org Mode formatter (round-trip):"
let org_formatted = format(org_data, 'org')
org_formatted

"test completed."
