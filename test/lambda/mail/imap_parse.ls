// lambda.mail.imap: mailbox names, LIST, SELECT and FETCH responses, UID sets
// (vibe/Lambda_Demo_Mail.md ML2). Responses are the tokens lambda.io.mail returns.
import imap: lambda.mail.imap

"=== modified UTF-7 mailbox names ===";
[for (path in ["INBOX", "&ZeVnLIqe-", "Entw&APw-rfe", "a&-b", "&2D3eAA-", "[Gmail]/&BBIERQQ+BDQETwRJBDgENQ-"])
    imap.mailbox_name(path)]

"=== LIST ==="
imap.list_entry(['LIST', ['\\HasNoChildren', '\\Sent'], "/", "[Gmail]/Sent Mail"])
imap.list_entry(['LIST', ['\\Noselect', '\\HasChildren'], "/", "[Gmail]"])
imap.list_entry(['LIST', [], ".", 'INBOX'])
imap.is_list(['LIST', [], "/", "x"])
imap.is_list([3, 'EXISTS'])

"=== SELECT ==="
imap.select_state({status: 'ok', code: ['READ-WRITE'], text: "done", responses: [
    ['FLAGS', ['\\Seen', '\\Flagged']],
    [4, 'EXISTS'],
    ['OK', ['UIDVALIDITY', 1700000001], "UIDs valid"],
    ['OK', ['UIDNEXT', 5], "next"],
    ['OK', ['HIGHESTMODSEQ', 912377], "modseq"]]})
imap.select_state({status: 'ok', code: ['READ-ONLY'], text: "", responses: []})

"=== FETCH ==="
let fetched = [12, 'FETCH', ['UID', 48213, 'FLAGS', ['\\Seen', '$Junk'], 'RFC822.SIZE', 48210,
    'INTERNALDATE', "21-Oct-2026 09:14:00 +0200", 'MODSEQ', [912370],
    'BODY[HEADER.FIELDS (DATE FROM SUBJECT)]', b'\x5375626A6563743A2068690D0A0D0A',
    'X-GM-MSGID', 1278455344230334865i64, 'X-GM-LABELS', ['\\Important', "Work"]]]
imap.is_fetch(fetched)
imap.fetch_items(fetched)

"=== UID sets ===";
[imap.uid_set([1, 2, 3, 7]), imap.uid_set([9, 4, 5, 4, 10, 11]), imap.uid_set([42]), imap.uid_set([])]
