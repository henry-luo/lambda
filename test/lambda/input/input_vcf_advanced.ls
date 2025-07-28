// Advanced vCard parser test with multiple contacts and complex properties
// Tests various vCard formats and edge cases

"\n=== Testing simple vCard parsing ==="
let simple_contact = input('./test/input/simple.vcf', 'vcf')
"Simple vCard result:"
simple_contact

"\n=== Testing complex vCard parsing ==="
let complex_contacts = input('./test/input/contacts.vcf', 'vcf')
"Complex vCard result (note: only first contact is parsed for now):"
complex_contacts

"\n=== Testing contact field extraction ==="
"Full Name: " + complex_contacts.full_name
"Primary Email: " + complex_contacts.email
"Primary Phone: " + complex_contacts.phone

"\n=== Testing structured data ==="
"Name structure:"
complex_contacts.name
"Address structure:"
complex_contacts.address

"\n=== Testing all properties access ==="
"All raw properties:"
complex_contacts.properties

"\n=== Testing property-specific access ==="
"Organization: " + complex_contacts.properties.org
"Title: " + complex_contacts.properties.title
"Birthday: " + complex_contacts.properties.bday
"Note: " + complex_contacts.properties.note

"\n=== Testing data structure validation ==="
"Contact has properties map: " + (type(complex_contacts.properties) == "map")
"Contact has name structure: " + (type(complex_contacts.name) == "map")
"Contact has address structure: " + (type(complex_contacts.address) == "map")
"Full name is string: " + (type(complex_contacts.full_name) == "string")

"\n=== Testing with different formats ==="
"JSON format:"
format(simple_contact, 'json')
"XML format:"
format(simple_contact, 'xml')
"YAML format:"
format(simple_contact, 'yaml')
"All vCard tests completed successfully!"
