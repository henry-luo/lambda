// Test for EML (Email Message File) parsing and formatting
// This script tests EML input parsing and various output formats

let eml_data = input('./test/input/test.eml', 'eml')

"EML parsing result:"
eml_data

"\nTesting top-level email fields:"
"From: " + eml_data.from
"To: " + eml_data.to  
"Subject: " + eml_data.subject
"Date: " + eml_data.date
"Message ID: " + eml_data.message_id

"\nTesting headers access:"
"MIME Version: " + eml_data.headers["mime-version"]
"Content Type: " + eml_data.headers["content-type"]
"X-Priority: " + eml_data.headers["x-priority"]
"Organization: " + eml_data.headers.organization

"\nTesting body content:"
"Body length: " + len(eml_data.body)
"Body preview: " + eml_data.body

"\nFormatting EML as JSON:"
format(eml_data, 'json')

"\nFormatting EML as XML:"
format(eml_data, 'xml')

"\nFormatting EML as HTML:"
format(eml_data, 'html')

"\nFormatting EML as Markdown:"
format(eml_data, 'markdown')

"\nFormatting EML as YAML:"
format(eml_data, 'yaml')

"\nFormatting EML as TOML:"
format(eml_data, 'toml')

"\nFormatting EML as INI:"
format(eml_data, 'ini')

"EML test completed!"
