// MIME decoding in the eml reader (vibe/Lambda_Demo_Mail.md MG5): RFC 2047
// headers, address lists, charsets to UTF-8 and the MIME part tree.

"=== encoded headers ==="
let encoded = input('./test/input/mail/encoded_headers.eml', 'eml')^
encoded.subject
encoded.addresses
encoded.mime

"=== multipart: alternative, related, RFC 2231 filename ==="
let mixed = input('./test/input/mail/multipart_mixed.eml', 'eml')^
mixed.mime

"=== legacy charsets ==="
let legacy = input('./test/input/mail/legacy_charsets.eml', 'eml')^
legacy.subject
legacy.addresses.from
legacy.mime

"=== format=flowed ==="
let flowed = input('./test/input/mail/flowed.eml', 'eml')^
flowed.mime

"=== malformed ==="
let broken = input('./test/input/mail/malformed.eml', 'eml')^
broken.subject
broken.mime
