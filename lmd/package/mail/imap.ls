// lambda.mail.imap — what IMAP responses from lambda.io.mail mean
// (vibe/Lambda_Demo_Mail.md ML2): mailbox names, LIST, SELECT and FETCH.
// Pure: the protocol itself stays in lambda.io.mail.

// Modified base64 of RFC 3501 §5.1.3 uses "," where base64 uses "/".
fn sextets(chunk) {
    let alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";
    [for (ch in split(chunk, "")) index_of(alphabet, ch)]
}

// The bytes a run of sextets carries: byte i spans bits 8i..8i+7 of the
// stream, inside the 12-bit window of the two sextets it touches.
fn sextet_bytes(values) {
    let total = int(floor(len(values) * 6 / 8));
    [for (i in range(0, total, 1)) {
        let first = int(floor(i * 8 / 6))
        let high = values[first]
        let low = if (first + 1 < len(values)) values[first + 1] else 0
        let divisor = [16, 4, 1][int((i * 8 % 6) / 2)];
        int(floor((high * 64 + low) / divisor)) % 256
    }]
}

// UTF-16BE code units to text; a high surrogate takes its low partner.
fn utf16_text(bytes) {
    let units = [for (i in range(0, int(floor(len(bytes) / 2)), 1)) bytes[2 * i] * 256 + bytes[2 * i + 1]];
    join([for (i, unit in units)
        if (unit >= 56320 and unit < 57344) ""
        else if (unit >= 55296 and unit < 56320 and i + 1 < len(units))
            chr(65536 + (unit - 55296) * 1024 + (units[i + 1] - 56320))
        else chr(unit)], "")
}

// A mailbox name as the server spells it (modified UTF-7) to text.
pub fn mailbox_name(path) {
    if (not contains(path, "&")) path
    else {
        let pieces = split(path, "&");
        join([for (i, piece in pieces) {
            let dash = index_of(piece, "-");
            if (i == 0) piece
            else if (dash == null) "&" ++ piece
            else if (dash == 0) "&" ++ slice(piece, 1)
            else utf16_text(sextet_bytes(sextets(slice(piece, 0, dash)))) ++ slice(piece, dash + 1)
        }], "")
    }
}

// A token as text: atoms arrive as symbols, literals as binary.
pub fn text_of(value) {
    if (value == null) null
    else if (value is binary) parse(value, 'text') ^ { null }
    else string(value)
}

fn lowered(flags) => [for (flag in flags) lower(string(flag))]

// RFC 6154 special-use roles; INBOX by name.
fn role_of(flags, path) {
    let names = lowered(flags);
    if (upper(path) == "INBOX") 'inbox'
    else if ("\\sent" in names) 'sent'
    else if ("\\drafts" in names) 'drafts'
    else if ("\\trash" in names) 'trash'
    else if ("\\junk" in names) 'junk'
    else if ("\\archive" in names) 'archive'
    else if ("\\all" in names) 'all'
    else if ("\\flagged" in names) 'flagged'
    else null
}

// One `* LIST (flags) "/" name` response.
pub fn list_entry(tokens) {
    let flags = tokens[1]
    let path = text_of(tokens[3])
    let names = lowered(flags);
    {path: path, name: mailbox_name(path), delimiter: text_of(tokens[2]), role: role_of(flags, path),
     selectable: not ("\\noselect" in names or "\\nonexistent" in names)}
}

pub fn is_list(tokens) => len(tokens) >= 4 and tokens[0] == 'LIST'

// The argument of an OK response code such as [UIDVALIDITY 3857529045].
fn code_value(responses, code_name) {
    let found = [for (r in responses where len(r) == 3 and r[0] == 'OK' and r[1] is array and
                     len(r[1]) > 1 and string(r[1][0]) == code_name) r[1][1]];
    if (len(found) == 0) null else found[len(found) - 1]
}

fn counted(responses, word) {
    let found = [for (r in responses where len(r) == 2 and r[1] is symbol and string(r[1]) == word) r[0]];
    if (len(found) == 0) null else found[len(found) - 1]
}

// What SELECT or EXAMINE reports about the mailbox.
pub fn select_state(result) => {
    uidvalidity: code_value(result.responses, "UIDVALIDITY"),
    uidnext: code_value(result.responses, "UIDNEXT"),
    highestmodseq: code_value(result.responses, "HIGHESTMODSEQ"),
    exists: counted(result.responses, "EXISTS"),
    read_only: result.code != null and len(result.code) > 0 and string(result.code[0]) == "READ-ONLY"
}

pub fn is_fetch(tokens) => len(tokens) == 3 and tokens[1] == 'FETCH' and tokens[2] is array

fn item(pairs, key) {
    let found = [for (p in pairs where p.key == key) p.value];
    if (len(found) == 0) null else found[0]
}

fn item_prefixed(pairs, prefix) {
    let found = [for (p in pairs where slice(p.key, 0, len(prefix)) == prefix) p.value];
    if (len(found) == 0) null else found[0]
}

// One `* n FETCH (...)` response as a map of its data items.
pub fn fetch_items(tokens) {
    let data = tokens[2]
    let pairs = [for (i in range(0, len(data) - 1, 2)) {key: lower(string(data[i])), value: data[i + 1]}]
    let modseq = item(pairs, "modseq");
    {seq: tokens[0],
     uid: item(pairs, "uid"),
     flags: if (item(pairs, "flags") == null) null else [for (f in item(pairs, "flags")) string(f)],
     size: item(pairs, "rfc822.size"),
     internaldate: item(pairs, "internaldate"),
     modseq: if (modseq is array and len(modseq) > 0) modseq[0] else null,
     header: item_prefixed(pairs, "body[header"),
     body: item(pairs, "body[]"),
     gm_msgid: item(pairs, "x-gm-msgid"),
     gm_thrid: item(pairs, "x-gm-thrid"),
     gm_labels: if (item(pairs, "x-gm-labels") == null) null else [for (l in item(pairs, "x-gm-labels")) text_of(l)]}
}

// UIDs as an IMAP sequence set: [1, 2, 3, 7] is "1:3,7".
pub fn uid_set(uids) {
    let sorted = sort(unique(uids));
    if (len(sorted) == 0) ""
    else join([for (i, uid in sorted where i == 0 or sorted[i - 1] != uid - 1) {
        let runs = [for (j in range(i, len(sorted), 1) where sorted[j] - uid == j - i) sorted[j]]
        let end_uid = runs[len(runs) - 1];
        if (end_uid == uid) string(uid) else string(uid) ++ ":" ++ string(end_uid)
    }], ",")
}
