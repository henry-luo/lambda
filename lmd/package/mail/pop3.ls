// lambda.mail.pop3 — a POP3 account as a download source (vibe/Lambda_Demo_Mail.md
// ML9): each message the server lists is downloaded once, found by its UIDL,
// into a local Inbox whose read state and flags live only in the store.
// Nothing is deleted on the server. Procedures over a lambda.io.mail session.
import wire: lambda.io.mail
import store: .store
import sync: .sync

// the local Inbox's mailbox name and store id; POP3 has no other mailbox
pub let INBOX = "INBOX"
// downloads between index saves, so an interrupted first sync keeps its progress
let BATCH = 25

// The only mailbox a POP3 account has, as lambda.mail.sync.mailboxes lists one.
pub fn mailboxes() => [{path: INBOX, name: INBOX, delimiter: null, role: 'inbox', selectable: true}]

// UIDL's listing ("1 uid-alpha\r\n2 uid-beta\r\n") as [{number, uidl}], in server order.
pub fn listing(text) => [for (line in split(replace(text, "\r\n", "\n"), "\n"),
    let words = [for (w in split(trim(line), " ") where w != "") w] where len(words) == 2)
    {number: int(words[0]), uidl: words[1]}]

// The listed messages the local entries lack. `except` settles the common
// case, nothing new, in one pass; the filter then scans only what is new.
pub fn missing(listed, entries) {
    let wanted = except([for (m in listed) m.uidl], [for (e in entries) e.uidl]);
    if (len(wanted) == 0) [] else [for (m in listed where m.uidl in wanted) m]
}

// A message's store id: its UIDL, which RFC 1939 limits to printable ASCII.
pub fn message_id(uidl) => "pop-" ++ store.safe_name(uidl)

// RETR's "+OK 1234 octets" reply: the size, or null
pub fn octets(text) => (let words = split(trim(text or ""), " "), int(words[0]) ^ { null })

// An empty local Inbox; local UIDs number downloads in arrival order.
pub fn new_index() => {version: 1, path: INBOX, uidvalidity: null, uidnext: 1, highestmodseq: null, entries: []}

// A multi-line payload as text: the bytes, or the file a large one streamed
// into; an empty payload is null (S2.2.2v2).
pn payload_text(payload) {
    if (payload == null) { return "" }
    if (payload is binary) { return parse(payload, 'text') ^ { "" } }
    return input(payload, 'text') ^ { "" }
}

pn saved(path, index, entries) map^ {
    let next = {*: index, uidnext: store.highest_uid({entries: entries}) + 1, entries: entries}
    store.save_index(path, next)^
    next
}

// Downloads the messages the server lists that the local Inbox lacks, in
// server order, and returns the saved index. A message gone from the server
// stays in the local Inbox. `progress`, when not null, receives the count
// downloaded so far after each message.
pub pn sync_inbox(session, root, progress) map^ {
    let index_file = store.index_path(root, INBOX)
    let loaded = store.load_index(index_file)
    var index = if (loaded == null) new_index() else loaded
    io.mkdir(store.incoming_dir(root))^
    let listed = wire.command(session, "UIDL", [], {to: store.incoming_dir(root)})^
    let wanted = missing(listing(payload_text(listed.responses[0])), index.entries)
    var entries = index.entries
    var done = 0
    for (m in wanted) {
        let reply = wire.command(session, "RETR", [m.number], {to: store.incoming_dir(root)})^
        let file = message_id(m.uidl)
        let message = sync.file_message(root, file, reply.responses[0])^
        let uid = store.highest_uid({entries: entries}) + 1
        entries = [*entries, {uid: uid, uidl: m.uidl, file: file, flags: [], *: sync.header_entry(message),
            *: sync.body_entry(message), internaldate: null, size: octets(reply.text), thread: null, has_file: true}]
        done = done + 1
        if (progress != null) { progress(done) }
        if (done % BATCH == 0) { index = saved(index_file, index, entries)^ }
    }
    saved(index_file, index, entries)
}

// The local Inbox with one message's read state changed: POP3 keeps none on
// the server, so the store is the only record (ML9).
pub pn mark_seen(root, uid, seen) map^ {
    let index_file = store.index_path(root, INBOX)
    let loaded = store.load_index(index_file)
    if (loaded == null) { return null }
    saved(index_file, loaded, store.with_flag(loaded.entries, uid, "\\Seen", seen))
}
