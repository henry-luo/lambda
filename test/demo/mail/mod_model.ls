// The mail demo's state and derivations, all pure (S12.1.1v3): accounts and
// their sync status, the sidebar, the message list and its scroll window,
// the open message, and the account sheet. Handlers in mail.ls do the IO and
// hand results here (vibe/Lambda_Demo_Mail.md §4, §5.5).
import prov: lambda.mail.providers
import mime: lambda.mail.mime

let MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]
// special-use mailboxes first, in Mac Mail's order (RFC 6154)
let ROLE_ORDER = ['inbox', 'drafts', 'sent', 'junk', 'trash', 'archive', 'all', 'flagged']
pub let ROLE_LABELS = {inbox: "Inbox", drafts: "Drafts", sent: "Sent", junk: "Junk", trash: "Trash",
    archive: "Archive", all: "All Mail", flagged: "Flagged"}
// list rows have one height, so a scroll offset names the rows in view
pub let ROW_HEIGHT = 64.0
// rows mounted above and below the viewport
let MARGIN_ROWS = 12

// `now` ({year, month, day}) dates the list's labels; handlers refresh it
pub fn initial() => {accounts: [], account: null, status: {}, mailboxes: {}, box: null, entries: [], counts: {},
    selected: null, fetching: null, remote: [], setup: null, preview: null, note: null, loaded: false,
    now: {year: 0, month: 0, day: 0}}

// ---- accounts ----

// a file-safe id for an address, distinct from the ids already taken
pub fn account_id(email, taken) {
    let base = join([for (ch in split(lower(email), "")) if (ch is \("\w") or ch == "-") ch else "-"], "")
    let free = [for (i in 1 to len(taken) + 1, let id = if (i == 1) base else base ++ "-" ++ string(i) where not (id in taken)) id];
    free[0]
}

pub fn account(st, id) => [for (a in st.accounts where a.id == id) a][0]
pub fn current(st) => account(st, st.account)

pub fn status_of(st, id) => st.status[id] or {state: "offline", text: null}
pub fn status_label(s) => if (s.text != null) s.text
    else {offline: "Offline", connecting: "Connecting…", syncing: "Syncing…", online: "Up to date", failed: "Failed"}[s.state] or s.state

// ---- mailboxes ----

// A mailbox's file name in the store: letters, digits and '-' stay; anything
// else is _code_, so two paths never share a name.
pub fn box_id(path) => join([for (ch in split(path, "")) if (ch is \("\w") and ch != "_" or ch == "-") ch else "_" ++ string(ord(ch)) ++ "_"], "")

fn depth(path, delimiter) => if (delimiter == null or delimiter == "") 0
    else len([for (ch in split(path, "") where ch == delimiter) ch])
fn leaf_name(name, delimiter) => if (delimiter == null or delimiter == "") name
    else (let parts = split(name, delimiter), parts[len(parts) - 1])
fn rank(b) => (let i = [for (j, r in ROLE_ORDER where b.role == r) j], if (len(i) == 0) len(ROLE_ORDER) else i[0])

pub fn count_key(account_id, path) => account_id ++ "/" ++ path

// The sidebar's rows for one account: special-use mailboxes first, then the
// rest by name, each with its unread count when its index is known.
pub fn sidebar(st, account_id) {
    let boxes = st.mailboxes[account_id] or [];
    [for (b in sort(boxes, (b) => [rank(b), lower(b.path)]) where b.selectable)
        (let c = st.counts[count_key(account_id, b.path)],
         {path: b.path, role: b.role, depth: if (b.role != null) 0 else depth(b.path, b.delimiter),
          name: if (b.role != null) ROLE_LABELS[b.role] else leaf_name(b.name, b.delimiter),
          unread: if (c == null) null else c.unread})]
}

pub fn inbox_path(boxes) => (let found = [for (b in boxes or [] where b.role == 'inbox') b.path], if (len(found) == 0) "INBOX" else found[0])

// ---- the message list ----

pub fn is_unread(e) => not ("\\Seen" in e.flags)
pub fn is_flagged(e) => "\\Flagged" in e.flags
pub fn counts_of(entries) => {unread: len([for (e in entries where is_unread(e)) e]), total: len(entries)}

// Rows are newest first: row i is entries[count - 1 - i]. `top` and `height`
// are the list's scroll offset and viewport in pixels.
pub fn window(count, top, height) {
    let first = max(0, int(floor(top / ROW_HEIGHT)) - MARGIN_ROWS)
    let stop = max(first, min(count, int(ceil((top + height) / ROW_HEIGHT)) + MARGIN_ROWS));
    {first: first, stop: stop, before: float(first) * ROW_HEIGHT, after: float(count - stop) * ROW_HEIGHT}
}
pub fn rows_in(entries, w) => [for (i in range(w.first, w.stop, 1)) entries[len(entries) - 1 - i]]
pub fn entry(st, uid) => [for (e in st.entries where e.uid == uid) e][0]

pub fn sender_label(e) => if (e.from == null) "(no sender)" else if (e.from.name != null and e.from.name != "") e.from.name else e.from.address
pub fn subject_label(subject) => if (subject == null or trim(subject) == "") "(no subject)" else subject

fn two(n) => if (n < 10) "0" ++ string(n) else string(n)
fn month_of(text) => (let i = [for (j, m in MONTHS where lower(m) == lower(slice(text, 0, 3))) j], if (len(i) == 0) null else i[0] + 1)

// The parts of an RFC 5322 date ("Thu, 15 Oct 2026 09:00:00 +0000") or an
// IMAP INTERNALDATE ("21-Oct-2026 09:14:00 +0200"); null when unreadable.
pub fn date_parts(text) {
    let words = if (text == null) [] else [for (w in split(replace(text, "-", " "), " ") where w != "" and not ends_with(w, ",")) w]
    let day = if (len(words) < 4) null else int(words[0]) ^ { null }
    let month = if (len(words) < 4) null else month_of(words[1])
    let year = if (len(words) < 4) null else int(words[2]) ^ { null };
    if (day == null or month == null or year == null) null
    else {year: if (year < 50) year + 2000 else if (year < 100) year + 1900 else year, month: month, day: day,
          time: slice(words[3], 0, 5)}
}

// "09:14" today, "21 Oct" this year, "21 Oct 2025" before; `now` is {year, month, day}
pub fn date_label(e, now) {
    let d = date_parts(e.date) or date_parts(e.internaldate);
    if (d == null) ""
    else if (d.year == now.year and d.month == now.month and d.day == now.day) d.time
    else if (d.year == now.year) string(d.day) ++ " " ++ MONTHS[d.month - 1]
    else string(d.day) ++ " " ++ MONTHS[d.month - 1] ++ " " ++ string(d.year)
}

pub fn size_label(bytes) => if (bytes == null) "" else if (bytes < 1024) string(bytes) ++ " B"
    else if (bytes < 1048576) string(int(round(bytes / 1024))) ++ " KB"
    else string(round(bytes / 104857.6) / 10) ++ " MB"

// ---- reports from an account's sync task (mod_sync.ls) ----

pub fn apply_report(st, m) {
    if (m.type == "status") {*: st, status: {*: st.status, [m.account]: {state: m.state, text: m.text}}}
    else if (m.type == "mailboxes") {
        let next = {*: st, mailboxes: {*: st.mailboxes, [m.account]: m.boxes}};
        if (m.account == st.account and not (st.box in [for (b in m.boxes) b.path])) {*: next, box: inbox_path(m.boxes)} else next
    }
    else if (m.type == "index") {
        let next = {*: st, counts: {*: st.counts, [count_key(m.account, m.path)]: counts_of(m.entries)}};
        if (m.account == st.account and m.path == st.box) {*: next, entries: m.entries} else next
    }
    else if (m.type == "failed") {*: st, fetching: null, note: m.text}
    else if (m.type == "autoconfig")
        (if (st.setup != null and st.setup.email == m.email) {*: st, setup: {*: st.setup, found: m.found, looking: false}} else st)
    else st
}

// ---- the open message ----

// What the viewer shows of a message read with input(…, 'eml'): headers,
// bodies, attachments and the parts its HTML names by cid:.
pub fn message_view(message, e, file) {
    let addr = message.addresses or {};
    {uid: e.uid, file: file, subject: subject_label(message.subject), date: message.date,
     from: addr.from or [], to: addr.to or [], cc: addr.cc or [],
     text: mime.text_body(message), html: mime.html_body(message),
     attachments: mime.attachments(message), cids: mime.cid_parts(message)}
}

pub fn address_label(a) => if (a.name != null and a.name != "") a.name ++ " <" ++ a.address ++ ">" else a.address
pub fn addresses_label(people) => join([for (a in people) address_label(a)], ", ")

// plain text as runs of lines at one quote depth ("> > " is depth 2)
fn quote_depth(line) => (let marks = [for (ch in split(replace(line, " ", ""), "")) ch], let n = [for (i, ch in marks where ch != ">") i],
    if (len(n) == 0) len(marks) else n[0])
fn unquoted(line, depth) => if (depth == 0) line else (let rest = [for (i, ch in split(line, "")) {i: i, ch: ch}],
    let seen = [for (r in rest where r.ch == ">") r.i], if (len(seen) < depth) line
    else (let cut = slice(line, seen[depth - 1] + 1), if (starts_with(cut, " ")) slice(cut, 1) else cut))
pub fn quote_runs(text) {
    let lines = split(replace(text or "", "\r\n", "\n"), "\n")
    let tagged = [for (line in lines) {depth: quote_depth(line), line: line}]
    let starts = [for (i, t in tagged where i == 0 or tagged[i - 1].depth != t.depth) i];
    [for (k, s in starts) (let end = if (k + 1 < len(starts)) starts[k + 1] else len(tagged),
        {depth: tagged[s].depth, text: join([for (i in range(s, end, 1)) unquoted(tagged[i].line, tagged[s].depth)], "\n")})]
}

// ---- the account sheet ----

pub fn setup_initial() => {name: "", email: "", password: "", provider: "auto", imap_host: "", imap_port: "993",
    imap_security: "tls", user: "", pin: "", smtp_host: "", smtp_port: "465", smtp_security: "tls", note: null, looking: false,
    found: null}

// the preset or the looked-up settings the sheet will use, or null for manual
pub fn setup_preset(form) => if (form.provider == "manual") null
    else if (form.provider == "auto") (prov.preset_for(form.email) or form.found)
    else prov.preset(form.provider)

fn security(text) => if (text == "starttls") 'starttls' else if (text == "plain") 'plain' else 'tls'
fn port(text, fallback) => int(trim(text or "")) ^ { fallback }

// An account from the sheet, or the reason it cannot be one. The password
// goes to the secret store, never into the account (ML6).
pub fn account_from_setup(form, taken) {
    let email = trim(form.email)
    let preset = setup_preset(form)
    let imap = if (preset != null) preset.imap else {host: trim(form.imap_host), port: port(form.imap_port, 993), security: security(form.imap_security)}
    let smtp = if (preset != null) preset.smtp else {host: trim(form.smtp_host), port: port(form.smtp_port, 465), security: security(form.smtp_security)}
    let user = if (trim(form.user) != "") trim(form.user) else if (preset != null and preset.user != null) preset.user else email;
    if (prov.domain_of(email) == "") {error: "Type an email address."}
    else if (form.password == "") {error: "Type the account's password or app password."}
    else if (imap.host == "") {error: "Choose a provider or type the IMAP server."}
    else {account: {id: account_id(email, taken), name: if (trim(form.name) != "") trim(form.name) else email, email: email,
        user: user, provider: if (preset != null) preset.id else "manual", imap: imap, smtp: smtp,
        pin: if (trim(form.pin) == "") null else trim(form.pin)}}
}
