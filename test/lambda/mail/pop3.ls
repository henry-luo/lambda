// lambda.mail.pop3 (vibe/Lambda_Demo_Mail.md ML9): the UIDL listing, what the
// local Inbox lacks, store ids, RETR sizes and the local Inbox. The sync
// itself runs against a fake server in test/mail (make test-mail).
import pop3: lambda.mail.pop3

"=== listing ==="
pop3.listing("1 uid-alpha\r\n2 uid-beta\r\n\r\n")
pop3.listing("3 a b\n4 only-four\n junk \n")
pop3.listing("")

"=== missing ==="
let listed = pop3.listing("1 a\r\n2 b\r\n3 c\r\n")
let some = [{uid: 1, uidl: "a"}, {uid: 2, uidl: "c"}, {uid: 3, uidl: "gone"}];
pop3.missing(listed, some)
pop3.missing(listed, [])
pop3.missing(listed, [{uidl: "a"}, {uidl: "b"}, {uidl: "c"}])

"=== ids and sizes ===";
[pop3.message_id("uid-alpha"), pop3.message_id("AB/cd+==.1"), pop3.octets("1234 octets"), pop3.octets(""), pop3.octets(null)]

"=== the local Inbox ==="
pop3.new_index()
pop3.mailboxes()
