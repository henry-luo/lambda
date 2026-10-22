// lambda.mail.mime: bodies, attachments, cid parts, snippets and search text
// from the eml reader's part tree (vibe/Lambda_Demo_Mail.md §4.4, §5.4.3).
import mime: lambda.mail.mime

let mixed = input('./test/input/mail/multipart_mixed.eml', 'eml')^
let flowed = input('./test/input/mail/flowed.eml', 'eml')^
let encoded = input('./test/input/mail/encoded_headers.eml', 'eml')^;

{multipart: {
    text: mime.text_body(mixed),
    html: mime.html_body(mixed),
    attachments: [for (a in mime.attachments(mixed)) {filename: a.filename, type: a.type, size: a.size}],
    cids: [for (p in mime.cid_parts(mixed)) {cid: p.cid, type: p.type}],
    snippet: mime.snippet(mixed, 40)},
 flowed: {text: mime.text_body(flowed), snippet: mime.snippet(flowed, 200)},
 search_text: mime.search_text(encoded),
 unflow: [mime.unflow("one \r\ntwo\r\n-- \r\nsig", false), mime.unflow("joined \r\nwords", true)]}
