// Preserve contact boundaries, repeated fields, and parameters in vCard input.
'=== vCard records ==='
let single = input('./test/input/simple.vcf', 'vcf')^
single.full_name == "John Doe"
len(single.entries) == 11

let group = input('./test/input/contacts.vcf', 'vcf')^
len(group.contacts) == 2
group.contacts[0].full_name == "Jane Smith"
group.contacts[1].full_name == "Bob Johnson"

let jane = group.contacts[0]
let emails = [for (entry in jane.entries where entry.name == "email") entry]
let phones = [for (entry in jane.entries where entry.name == "tel") entry]
len(emails) == 2
emails[0].value == "jane.smith@company.org"
emails[0].parameters.type == "work"
emails[1].parameters.type == "home"
len(phones) == 3
phones[2].parameters.type == "cell"

let bob = group.contacts[1]
len([for (entry in bob.entries where entry.name == "email") entry]) == 1
len([for (entry in bob.entries where entry.name == "x-twitter") entry]) == 0
