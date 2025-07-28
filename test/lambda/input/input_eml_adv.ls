// Comprehensive EML parser test with edge cases
// Tests various email formats and edge cases

"\n=== Testing basic EML parsing ==="
let simple_eml = input('./test/input/simple.eml', 'eml')
"Simple EML result:"
simple_eml

"\n=== Testing complex EML parsing ==="
let complex_eml = input('./test/input/test.eml', 'eml')
"Complex EML result:"
complex_eml

"\n=== Testing email address extraction ==="
"From email extracted: " + complex_eml.from
"To email extracted: " + complex_eml.to

"\n=== Testing header normalization ==="
"All headers are lowercase keys:"
complex_eml.headers

"\n=== Testing specific header access ==="
"MIME-Version: " + complex_eml.headers["mime-version"]
"Content-Type: " + complex_eml.headers["content-type"] 
"X-Mailer: " + complex_eml.headers["x-mailer"]
"User-Agent: " + complex_eml.headers["user-agent"]

"\n=== Testing continuation headers ==="
"Received header (should be on single line): " + complex_eml.headers.received

"\n=== Testing body parsing ==="
"Body starts with: " + complex_eml.body
"Body length: " + len(complex_eml.body)

"\n=== Testing data structure ==="
"Email has headers map: " + (type(complex_eml.headers) == "map")
"Email has string fields: " + (type(complex_eml.subject) == "string")
"Email body is string: " + (type(complex_eml.body) == "string")

"\n=== Testing with different formats ==="
"JSON format:"
format(simple_eml, 'json')
"XML format:"
format(simple_eml, 'xml')
"YAML format:"
format(simple_eml, 'yaml')
"All EML tests completed successfully!"
