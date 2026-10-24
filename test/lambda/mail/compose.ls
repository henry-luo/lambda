// lambda.mail.compose: reply and forward drafts, quoting, the HTML of a
// plain-text body, typed recipients, and the RFC 5322 text of a draft
// (vibe/Lambda_Demo_Mail.md §4.6); mime.html_text for HTML-only bodies.
import compose: lambda.mail.compose
import mime: lambda.mail.mime

let me = {name: "Alex Rivera", address: "alex@lambda.demo"}
let m = {from: [{name: "Maya Chen", address: "maya@x.example"}], to: [me, {name: "José Álvarez", address: "jose@y.example"}],
    cc: [{name: null, address: "MAYA@x.example"}, {name: "Priya", address: "priya@z.example"}], reply_to: [],
    subject: "Re: Offsite", date: "Sat, 24 Oct 2026 02:19:59 +0000", message_id: "<a@x>", references: "<0@x>",
    text: "Hi\n> earlier\n> > older\nBye", attachments: [{filename: "a.txt", type: "text/plain", content: "x"}]};

"=== subjects ===";
[compose.reply_subject("Offsite"), compose.reply_subject("RE: Offsite"), compose.reply_subject(null),
 compose.forward_subject("Offsite"), compose.forward_subject("Fw: Offsite")]

"=== quoting ==="
compose.quote("one\n> two")
compose.quote_runs("Thanks!\n> first\n>> deeper\nend")
compose.html_of_text(m.text)
compose.html_of_text("a < b & c")

"=== replies ==="
compose.reply(m, me, false)
compose.reply(m, me, true);
[compose.reply({*: m, from: [me]}, me, false).to, compose.reply({*: m, reply_to: [{name: "List", address: "list@x"}]}, me, false).to]
let fwd = compose.forward(m);
[fwd.subject, len(fwd.attachments), fwd.text]

"=== recipients ==="
compose.recipients("\"Ann Lee\" <ann@x.org>, bo@y.org, José <j@z.org>")
compose.recipients("  ");
[compose.problem({*: compose.blank(), to: "nobody"}), compose.problem({*: compose.blank(), to: ""}),
 compose.problem({*: compose.blank(), to: "a@b.c"})]

"=== eml ==="
compose.rfc5322_date(1761272399000)
let draft = {*: compose.reply(m, me, true), text: "Sounds good.\n\n" ++ compose.reply(m, me, true).text};
let text = compose.eml(draft, me, "Sat, 24 Oct 2026 03:00:00 +0000", "<r1@lambda.demo>")^;
// verify wire CRLF before rendering an LF-normalized golden.
let lf = replace(text, "\r\n", "\n");
[contains(text, "\r\n\r\n"), not contains(lf, "\r"), len(split(lf, "\n")) == len(split(text, "\r\n"))]
lf
let sent = parse(binary(text), 'eml')^;
[sent.subject, sent.addresses.to, sent.headers["in-reply-to"], replace(mime.text_body(sent), "\r\n", "\n") == draft.text, mime.html_body(sent) != null]

"=== html text ==="
mime.html_text("<html><head><title>T</title><style>p{color:red}</style></head><body><h1>Hi&nbsp;there</h1><p>One &amp; two</p><div>a<br>b</div></body></html>")
