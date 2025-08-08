// Simple Org-mode roundtrip test
"=== Testing Org-mode roundtrip ==="
let org_text = input('./test/input/test.org', 'text')

// Parse the Org-mode file
let org_data = input('./test/input/test.org', 'org')

"=== PARSED STRUCTURE ==="
org_data

"=== FORMATTED BACK ==="
let formatted = format(org_data, 'org')
formatted

"=== ROUND-TRIP Comparison ==="
"len(org_text):"; len(org_text)
"len(formatted):"; len(formatted)
org_text == formatted

"Org-mode parsing and formatting test completed successfully"
