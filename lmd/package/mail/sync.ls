// lambda.mail.sync — an account's mailboxes into the store: headers first,
// bodies on demand (vibe/Lambda_Demo_Mail.md §5.4.2). Procedures over a
// lambda.io.mail session; the window runs them in its sync task.
import wire: lambda.io.mail
import imap: .imap
import store: .store
import mime: .mime

// The headers a list row needs; parse(…, 'eml') decodes them (§5.4.2).
fn header_fields() => "DATE FROM TO CC SUBJECT MESSAGE-ID IN-REPLY-TO REFERENCES"

// The capability atoms, lower case.
pub pn capabilities(session) array^ {
    let reply = wire.command(session, "CAPABILITY")^
    let lines = [for (r in reply.responses where len(r) > 0 and r[0] == 'CAPABILITY') r];
    if (len(lines) == 0) [] else [for (atom in lines[0] where atom != 'CAPABILITY') lower(string(atom))]
}

// Every mailbox of the account.
pub pn mailboxes(session) array^ {
    let listed = wire.command(session, "LIST", ["", "*"])^;
    [for (r in listed.responses where imap.is_list(r)) imap.list_entry(r)]
}

// A list row's header fields from a message read with parse(…, 'eml'),
// or from none: an IMAP header block, or a whole POP3 download.
pub fn header_entry(header) {
    let senders = if (header == null or header.addresses.from == null) [] else header.addresses.from;
    {date: if (header == null) null else header.date,
     from: if (len(senders) == 0) null else senders[0],
     to: if (header == null or header.addresses.to == null) [] else header.addresses.to,
     subject: if (header == null) null else header.subject,
     message_id: if (header == null) null else header.message_id}
}

// One list row from a FETCH of the header fields.
fn entry_of(box_id, uidvalidity, item) {
    let header = if (item.header == null) null else (parse(item.header, 'eml') ^ { null });
    {uid: item.uid,
     file: store.server_id(box_id, uidvalidity, item),
     flags: if (item.flags == null) [] else item.flags,
     *: header_entry(header),
     internaldate: item.internaldate,
     size: item.size,
     thread: item.gm_thrid,
     has_file: false}
}

// Brings one mailbox's index up to date, saves it and returns it: new
// messages' headers, known messages' flags, expunged ones dropped. A changed
// UIDVALIDITY starts the index over. `gmail` adds X-GM-MSGID and X-GM-THRID,
// which name a message across Gmail's label mailboxes.
pub pn sync_mailbox(session, root, box_id, path, gmail) map^ {
    let index_file = store.index_path(root, box_id)
    let selected = wire.command(session, "SELECT", [path])^
    let selected_state = imap.select_state(selected)
    let loaded = store.load_index(index_file)
    let index = if (loaded == null or loaded.uidvalidity != selected_state.uidvalidity) store.new_index(path, selected_state) else loaded
    let next_uid = store.highest_uid(index) + 1
    var entries = index.entries
    if (len(entries) > 0) {
        let refreshed = wire.command(session, "UID FETCH 1:" ++ string(next_uid - 1) ++ " (UID FLAGS)")^
        entries = store.refresh_entries(entries, [for (r in refreshed.responses where imap.is_fetch(r)) imap.fetch_items(r)])
    }
    if (selected_state.exists != null and selected_state.exists > 0) {
        let extra = if (gmail) " X-GM-MSGID X-GM-THRID" else ""
        let fetched = wire.command(session, "UID FETCH " ++ string(next_uid) ++ ":* (UID FLAGS RFC822.SIZE INTERNALDATE" ++
            extra ++ " BODY.PEEK[HEADER.FIELDS (" ++ header_fields() ++ ")])")^
        let items = [for (r in fetched.responses where imap.is_fetch(r)) imap.fetch_items(r)]
        // "n:*" names the last message even when nothing is newer than n
        let fresh = [for (item in items where item.uid >= next_uid) entry_of(box_id, selected_state.uidvalidity, item)]
        entries = store.merge_entries(entries, fresh)
    }
    let updated = store.with_state(index, selected_state, entries)
    store.save_index(index_file, updated)^
    updated
}

// Files a downloaded message under `id` with its search sidecar and returns
// it read with input(…, 'eml'). `body` is the bytes, or the path of a file
// that a large download streamed into `incoming/` (§5.3), which is moved.
pub pn file_message(root, id, body) map^ {
    let path = store.message_path(root, id)
    io.mkdir(store.message_dir(root, id))^
    if (body is binary) { output(body, path)^ } else { io.move(body, path)^ }
    let message = input(path, 'eml')^
    output(mime.search_text(message), store.text_path(root, id))^
    message
}

// Downloads one message of the selected mailbox into the store, with its
// search sidecar, and returns the file's path.
pub pn fetch_message(session, root, entry) string^ {
    let path = store.message_path(root, entry.file)
    if (exists(path)) { return path }
    io.mkdir(store.incoming_dir(root))^
    let fetched = wire.command(session, "UID FETCH", [entry.uid, ['BODY.PEEK[]']], {to: store.incoming_dir(root)})^
    let items = [for (r in fetched.responses where imap.is_fetch(r)) imap.fetch_items(r)]
    let body = if (len(items) == 0) null else items[0].body
    if (body == null) { raise error("lambda.mail: the server sent no body for UID " ++ string(entry.uid)) }
    file_message(root, entry.file, body)^
    path
}
