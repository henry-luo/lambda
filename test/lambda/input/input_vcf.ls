// Test for vCard (Virtual Contact File) parsing and formatting
// This script tests vCard input parsing and various output formats

let contact_data = input('./test/input/simple.vcf', 'vcf')
"vCard parsing result:"
contact_data

"\nTesting top-level contact fields:"
"Full Name: " + contact_data.full_name
"Email: " + contact_data.email
"Phone: " + contact_data.phone
"Organization: " + contact_data.organization
"Title: " + contact_data.title
"Birthday: " + contact_data.birthday
"Note: " + contact_data.note
"URL: " + contact_data.url
"Version: " + contact_data.version

"\nTesting structured name access:"
"Family Name: " + contact_data.name.family
"Given Name: " + contact_data.name.given
"Additional Names: " + contact_data.name.additional
"Name Prefix: " + contact_data.name.prefix
"Name Suffix: " + contact_data.name.suffix

"\nTesting structured address access:"
"Street: " + contact_data.address.street
"City: " + contact_data.address.city
"State: " + contact_data.address.state
"Postal Code: " + contact_data.address.postal_code
"Country: " + contact_data.address.country

"\nTesting properties access:"
"All properties:"
contact_data.properties

"\nTesting specific property access:"
"EMAIL property: " + contact_data.properties.email
"TEL property: " + contact_data.properties.tel
"ORG property: " + contact_data.properties.org

"\nFormatting vCard as JSON:"
format(contact_data, 'json')

"\nFormatting vCard as XML:"
format(contact_data, 'xml')

"\nFormatting vCard as YAML:"
format(contact_data, 'yaml')

"\nFormatting vCard as TOML:"
format(contact_data, 'toml')

"\nFormatting vCard as INI:"
format(contact_data, 'ini')

"vCard test completed!"
