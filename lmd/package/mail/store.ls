// lambda.mail.store — the mail store of ML7v3 (vibe/Lambda_Demo_Mail.md §5.4):
// raw .eml files written once, plain-text sidecars for search, and one Mark
// index per mailbox saved whole and atomically.
//
// <store>/index/<mailbox-id>.mark
// <store>/messages/<nn>/<server-id>.eml and .txt
// <store>/incoming/                      fetches in flight

pub fn index_path(store, box_id) => store ++ "/index/" ++ box_id ++ ".mark"
pub fn incoming_dir(store) => store ++ "/incoming"

// A name that is safe in a file name and distinct for distinct text, on a
// case-insensitive file system too: lower-case letters, digits and '-' stay,
// a capital is '_' and its lower case, and any other character is _code_.
pub fn safe_name(text) => join([for (ch in split(text, ""))
    if (ch is \("a" to "z" | "0" to "9" | "-")) ch
    else if (ch is \("A" to "Z")) "_" ++ lower(ch)
    else "_" ++ string(ord(ch)) ++ "_"], "")

// The last two characters of the id keep a directory small ("-3" reads as "03").
fn shard(id) => replace(if (len(id) < 2) "0" ++ id else slice(id, len(id) - 2), "-", "0")

pub fn message_dir(store, id) => store ++ "/messages/" ++ shard(id)
pub fn message_path(store, id) => message_dir(store, id) ++ "/" ++ id ++ ".eml"
pub fn text_path(store, id) => message_dir(store, id) ++ "/" ++ id ++ ".txt"

// A message's identity: Gmail's X-GM-MSGID names it in every label mailbox;
// elsewhere the mailbox, its UIDVALIDITY and the UID do (§5.4.1).
pub fn server_id(box_id, uidvalidity, item) {
    if (item.gm_msgid != null) "gm" ++ string(item.gm_msgid)
    else box_id ++ "-" ++ string(uidvalidity) ++ "-" ++ string(item.uid)
}

fn parent(path) {
    let cut = [for (i in range(0, len(path), 1) where slice(path, i, i + 1) == "/") i];
    if (len(cut) == 0) "." else slice(path, 0, cut[len(cut) - 1])
}

// An index after a SELECT: the mailbox's state and its entries in UID order.
pub fn new_index(path, selected_state) => {
    version: 1, path: path, uidvalidity: selected_state.uidvalidity, uidnext: selected_state.uidnext,
    highestmodseq: selected_state.highestmodseq, entries: []
}

pub fn with_state(index, selected_state, entries) => {
    version: 1, path: index.path, uidvalidity: selected_state.uidvalidity, uidnext: selected_state.uidnext,
    highestmodseq: selected_state.highestmodseq, entries: entries
}

pub fn highest_uid(index) => if (index == null or len(index.entries) == 0) 0 else index.entries[len(index.entries) - 1].uid

pub pn load_index(path) {
    if (not exists(path)) { return null }
    input(path, 'mark') ^ { null }
}

// Whole and atomic: a crash leaves the old index or the new one (§5.4.2).
pub pn save_index(path, index) string^ {
    io.mkdir(parent(path))^
    output(index, path, {atomic: true})^
    path
}

// Entries by UID: new ones replace old ones; the result stays in UID order.
pub pn merge_entries(entries, fresh) {
    var replaced = {}
    for (e in fresh) { replaced = {*: replaced, [string(e.uid)]: true} }
    let kept = [for (e in entries where replaced[string(e.uid)] == null) e]
    sort([*kept, *fresh], (e) => e.uid)
}

// The flags a refresh reported; entries it no longer lists were expunged.
pub pn refresh_entries(entries, items) {
    var flags = {}
    for (item in items) { flags = {*: flags, [string(item.uid)]: item.flags} };
    [for (e in entries where flags[string(e.uid)] != null)
        {*: e, flags: flags[string(e.uid)]}]
}

// Marks entries whose message file is now on disk.
pub pn with_files(entries, uids) {
    var stored = {}
    for (uid in uids) { stored = {*: stored, [string(uid)]: true} };
    [for (e in entries) if (stored[string(e.uid)] != null) {*: e, has_file: true} else e]
}
