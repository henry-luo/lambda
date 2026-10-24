// The mail demo's state and derivations, all pure (S12.1.1v3): accounts and
// their sync status, the sidebar, the message list and its scroll window,
// the open message, and the account sheet. Handlers in mail.ls do the IO and
// hand results here (vibe/Lambda_Demo_Mail.md §4, §5.5).
import prov: lambda.mail.providers
import mime: lambda.mail.mime
import store: lambda.mail.store
import compose: lambda.mail.compose

let MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]
// special-use mailboxes first, in Mac Mail's order (RFC 6154)
let ROLE_ORDER = ['inbox', 'drafts', 'sent', 'junk', 'trash', 'archive', 'all', 'flagged']
pub let ROLE_LABELS = {inbox: "Inbox", drafts: "Drafts", sent: "Sent", junk: "Junk", trash: "Trash",
    archive: "Archive", all: "All Mail", flagged: "Flagged"}
// list rows have one height, so a scroll offset names the rows in view
pub let ROW_HEIGHT = 86.0
// rows mounted above and below the viewport
let MARGIN_ROWS = 12

// `now` ({year, month, day}) dates the list's labels; handlers refresh it
pub fn initial() => {accounts: [], account: null, status: {}, mailboxes: {}, box: null, entries: [], counts: {},
    selected: null, fetching: null, remote: [], setup: null, preview: null, note: null, loaded: false, clients: null,
    confirm_remove: null, collapsed: [], new_box: null, compose: null,
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

// A mailbox's file name in the store; two paths never share one.
pub fn box_id(path) => store.safe_name(path)

fn depth(path, delimiter) => if (delimiter == null or delimiter == "") 0
    else len([for (ch in split(path, "") where ch == delimiter) ch])
fn leaf_name(name, delimiter) => if (delimiter == null or delimiter == "") name
    else (let parts = split(name, delimiter), parts[len(parts) - 1])
fn rank(b) => (let i = [for (j, r in ROLE_ORDER where b.role == r) j], if (len(i) == 0) len(ROLE_ORDER) else i[0])

pub fn count_key(account_id, path) => account_id ++ "/" ++ path

// a mailbox's ancestors, nearest last ("A/B/C" → ["A", "A/B"])
fn ancestors(path, delimiter) => if (delimiter == null or delimiter == "") []
    else (let parts = split(path, delimiter), [for (i in range(1, len(parts), 1)) join(slice(parts, 0, i), delimiter)])

// the other mailboxes of a folder tree under `b`
fn children(boxes, b) => [for (c in boxes where c.role == null and c.path != b.path and b.delimiter != null and b.delimiter != "" and
    starts_with(c.path, b.path ++ b.delimiter)) c]

// sidebar keys of collapsed accounts and folders ("acct", "acct/Projects")
pub fn toggled(keys, key) => if (key in keys) [for (k in keys where k != key) k] else [*keys, key]
pub fn is_collapsed(st, key) => key in (st.collapsed or [])

// The sidebar's rows for one account: special-use mailboxes first, then the
// rest by name as a tree, each with its unread count when its index is known.
// A folder with children has a disclosure toggle; the rows under a collapsed
// one are left out. A parent that cannot be selected stays as a label.
pub fn sidebar(st, account_id) {
    let boxes = st.mailboxes[account_id] or [];
    [for (b in sort(boxes, (b) => [rank(b), lower(b.path)]),
          let kids = if (b.role != null) [] else children(boxes, b)
          where (b.selectable or len(kids) > 0) and (b.role != null or
              len([for (a in ancestors(b.path, b.delimiter) where is_collapsed(st, count_key(account_id, a))) a]) == 0))
        (let c = st.counts[count_key(account_id, b.path)],
         {path: b.path, role: b.role, depth: if (b.role != null) 0 else depth(b.path, b.delimiter),
          name: if (b.role != null) ROLE_LABELS[b.role] else leaf_name(b.name, b.delimiter),
          unread: if (c == null) null else c.unread, selectable: b.selectable, parent: len(kids) > 0,
          collapsed: is_collapsed(st, count_key(account_id, b.path))})]
}

// the role of the shown mailbox, or null
pub fn box_role(st) => (let found = [for (b in st.mailboxes[st.account] or [] where b.path == st.box) b.role], if (len(found) == 0) null else found[0])
pub fn box_title(st) => (let role = box_role(st), if (role != null) ROLE_LABELS[role]
    else if (st.box == null) "Mail" else (let named = [for (b in st.mailboxes[st.account] or [] where b.path == st.box) b],
        if (len(named) == 0) st.box else leaf_name(named[0].name, named[0].delimiter)))

// the path of the account's mailbox with `role`, or null
pub fn role_path(st, account_id, role) => (let found = [for (b in st.mailboxes[account_id] or [] where b.role == role) b.path],
    if (len(found) == 0) null else found[0])

// where a new mailbox can go: the account's top level, or under a folder
pub fn mailbox_parents(st, account_id) => [["", "Top level"],
    *[for (b in sort(st.mailboxes[account_id] or [], (b) => lower(b.path)) where b.role == null and b.selectable) [b.path, b.path]]]

// the path of a new mailbox named `name` under `parent`, or the reason it cannot be one
pub fn new_mailbox_path(st, account_id, parent, name) {
    let leaf = trim(name or "")
    let path = if (parent == null or parent == "") leaf else parent ++ "/" ++ leaf;
    if (leaf == "") {error: "Type a name for the mailbox."}
    else if (contains(leaf, "/")) {error: "A mailbox name cannot contain “/”."}
    else if (path in [for (b in st.mailboxes[account_id] or []) b.path]) {error: "There is already a mailbox named “" ++ path ++ "”."}
    else {path: path}
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
fn short_name(a) => if (a.name != null and a.name != "") a.name else a.address
// who a row names: the recipients in Sent and Drafts, else the sender
pub fn who_label(e, role) => if (role == 'sent' or role == 'drafts')
    (let to = e.to or [], if (len(to) == 0) "(no recipients)" else "To: " ++ join([for (a in to) short_name(a)], ", "))
    else sender_label(e)

// The row to show after `uid` leaves the list: the next older one, else the newer one.
pub fn neighbor(entries, uid) {
    let at = [for (i, e in entries where e.uid == uid) i];
    if (len(at) == 0) null else if (at[0] > 0) entries[at[0] - 1].uid else if (len(entries) > 1) entries[1].uid else null
}
pub fn without_entry(st, uid) => {*: st, entries: [for (e in st.entries where e.uid != uid) e]}
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

// "24 Oct 2026 at 09:14" for the viewer's header
pub fn long_date(text) => (let d = date_parts(text), if (d == null) (text or "")
    else string(d.day) ++ " " ++ MONTHS[d.month - 1] ++ " " ++ string(d.year) ++ " at " ++ d.time)

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
    let addr = message.addresses or {}
    let headers = message.headers or {};
    {uid: e.uid, file: file, subject: subject_label(message.subject), date: message.date,
     from: addr.from or [], to: addr.to or [], cc: addr.cc or [], reply_to: addr.reply_to or [],
     message_id: message.message_id, references: headers.references, flags: e.flags,
     text: mime.text_body(message), html: mime.html_body(message), readable: mime.readable_text(message),
     attachments: mime.attachments(message), cids: mime.cid_parts(message)}
}

pub fn address_label(a) => compose.person_label(a)
pub fn addresses_label(people) => compose.people_label(people)
pub fn quote_runs(text) => compose.quote_runs(text)

// two letters for a sender's avatar, and a hue picked from the address
pub fn initials(a) => (let words = [for (w in split(trim(short_name(a)), " ") where w != "" and not starts_with(w, "(")) w],
    if (len(words) == 0) "?" else if (len(words) == 1) upper(slice(words[0], 0, 1))
    else upper(slice(words[0], 0, 1) ++ slice(words[len(words) - 1], 0, 1)))
pub fn avatar_hue(a) => sum([for (ch in split(lower(a.address or "?"), "")) ord(ch)]) * 47 % 360

// ---- compose ----

// The compose window's state over a draft (lambda.mail.compose): `mode` is
// new, reply, reply-all, forward or draft; `replaces` the Drafts UID it updates.
pub fn compose_window(draft, mode, account_id, replaces) => {*: draft, mode: mode, account: account_id, replaces: replaces,
    note: null, sending: false}

// the message as a reply or forward takes it: its readable text for quoting
pub fn for_reply(m) => {*: m, text: m.readable}

// who an account's mail is from: its owner's name where the account has one
pub fn sender_of(a) => {name: a.full_name or a.name, address: a.email}

pub fn compose_title(c) => if (trim(c.subject or "") != "") trim(c.subject) else "New Message"

// a draft read back from the Drafts mailbox, for editing
pub fn draft_of(m) => {*: compose.blank(), to: compose.people_label(m.to), cc: compose.people_label(m.cc),
    subject: if (m.subject == "(no subject)") "" else m.subject, text: m.text or m.readable, attachments: m.attachments}

// ---- the account sheet ----

// The sheet's incoming server is IMAP or POP3 (ML9); its port defaults by protocol and security.
// `auth` is the sign-in the person chose, null until they choose (setup_auth).
pub fn setup_initial() => {name: "", email: "", password: "", provider: "auto", protocol: "imap", host: "",
    port: "", security: "tls", user: "", pin: "", smtp_host: "", smtp_port: "465", smtp_security: "tls",
    auth: null, note: null, notice: null, looking: false, found: null, signing: false, signing_with: null, signer: null,
    google_setup: false, client_id: "", client_secret: ""}

// the preset or the looked-up settings the sheet will use, or null for manual
pub fn setup_preset(form) => if (form.provider == "manual") null
    else if (form.provider == "auto") (prov.preset_for(form.email) or form.found)
    else prov.preset(form.provider)

fn security(text) => if (text == "starttls") 'starttls' else if (text == "plain") 'plain' else 'tls'
fn port(text, fallback) => int(trim(text or "")) ^ { fallback }
fn default_port(protocol, sec) => if (protocol == "pop3") (if (sec == 'tls') 995 else 110) else (if (sec == 'tls') 993 else 143)

// The OAuth endpoints the sheet's provider offers when the app has a client
// for them in oauth.mark (MQ4), else null.
pub fn setup_oauth(form, clients) {
    let preset = setup_preset(form);
    if (preset == null or preset.oauth == null or clients == null or clients[preset.oauth.provider] == null) null
    else preset.oauth
}

// "oauth" or "password": OAuth by default where it is offered
pub fn setup_auth(form, clients) => if (setup_oauth(form, clients) == null) "password"
    else if (form.auth == null) "oauth" else form.auth

// the app's client for a provider, as lambda.oauth takes it, or null
pub fn oauth_client(clients, endpoints) => if (endpoints == null or clients == null or clients[endpoints.provider] == null) null
    else (let c = clients[endpoints.provider], {id: c.client_id, secret: c.client_secret})

// An account from the sheet, or the reason it cannot be one. The password
// or refresh token goes to the secret store, never into the account (ML6).
pub fn account_from_setup(form, taken, clients) {
    let email = trim(form.email)
    let preset = setup_preset(form)
    let pop = form.protocol == "pop3"
    let sec = security(form.security)
    let manual = {host: trim(form.host), port: port(form.port, default_port(form.protocol, sec)), security: sec}
    let incoming = if (preset == null) manual else if (pop) preset.pop3 else preset.imap
    let smtp = if (preset != null) preset.smtp else {host: trim(form.smtp_host), port: port(form.smtp_port, 465), security: security(form.smtp_security)}
    let user = if (trim(form.user) != "") trim(form.user) else if (preset != null and preset.user != null) preset.user else email
    let auth = setup_auth(form, clients);
    if (prov.domain_of(email) == "") {error: "Type an email address."}
    else if (auth == "password" and form.password == "") {error: "Type the account's password or app password."}
    else if (incoming == null) {error: preset.name ++ " offers no " ++ (if (pop) "POP3" else "IMAP") ++ "; choose the other protocol."}
    else if (incoming.host == "") {error: "Choose a provider or type the incoming server."}
    else {account: {id: account_id(email, taken), name: if (trim(form.name) != "") trim(form.name) else email, email: email,
        user: user, provider: if (preset != null) preset.id else "manual", protocol: if (pop) "pop3" else "imap",
        imap: if (pop) null else incoming, pop3: if (pop) incoming else null, smtp: smtp,
        pin: if (trim(form.pin) == "") null else trim(form.pin),
        auth: auth, oauth: if (auth == "oauth") setup_oauth(form, clients) else null}}
}

pub fn is_pop3(a) => a != null and a.protocol == "pop3"
// the demo account (mod_demo.ls): a seeded local store, no server
pub fn is_demo(a) => a != null and a.protocol == "demo"
// read state the store keeps on its own, since no server records it (ML9)
pub fn reads_locally(a) => is_pop3(a) or is_demo(a)
// flags, moves and new mailboxes kept in the local store; IMAP's wait for M2
pub fn organizes_locally(a) => is_demo(a)

// The account a "Login with Gmail" sign-in for `email` makes. An account for
// the same address turns to OAuth, keeping its id, its store and, if it was
// Gmail already, its protocol; any other address is a new Gmail account.
// `endpoints` are the provider's (lambda.mail.providers GOOGLE).
pub fn gmail_account(email, name, accounts, endpoints) {
    let preset = prov.preset("gmail")
    let same = [for (a in accounts where lower(a.email) == lower(email)) a][0]
    let keep = same != null and same.provider == "gmail";
    {id: if (same != null) same.id else account_id(email, [for (a in accounts) a.id]),
     name: if (same != null) same.name else if (name != null and name != "") name else email,
     email: email, user: email, provider: "gmail",
     protocol: if (keep) same.protocol else "imap",
     imap: if (keep and same.protocol == "pop3") null else preset.imap,
     pop3: if (keep and same.protocol == "pop3") preset.pop3 else null,
     smtp: preset.smtp, pin: null, auth: "oauth", oauth: endpoints}
}

// the accounts with `a` in place of the account of the same id, or added
pub fn with_account(accounts, a) => if (len([for (x in accounts where x.id == a.id) x]) > 0)
    [for (x in accounts) if (x.id == a.id) a else x] else [*accounts, a]

pub fn without_account(accounts, id) => [for (x in accounts where x.id != id) x]

// The whole shape of a Google client, so a stray character pasted with the ID
// or the secret is caught here: Google only answers "OAuth client was not found"
type GoogleClientId = \("\d"+ "-" ("a" to "z" | "\d")+ ".apps.googleusercontent.com")
type ClientSecret = \(("\w" | "-")+)

pub fn google_client_error(client_id, client_secret) {
    let id = trim(client_id or "")
    let secret = trim(client_secret or "");
    if (id == "") "Paste the client ID, which ends in .apps.googleusercontent.com."
    else if (not (id is GoogleClientId)) "That is not a whole client ID. Paste only the ID, like 123456789012-abc….apps.googleusercontent.com, with nothing before or after it."
    else if (secret == "") "Paste the client secret too."
    else if (not (secret is ClientSecret)) "That is not a whole client secret. Paste only the secret, with nothing before or after it."
    else null
}

// the Google setup panel over the sheet's form, holding the saved client, if
// any, for correcting
pub fn google_setup_form(form, client, note) => {*: form, google_setup: true,
    client_id: if (client == null) "" else (client.id or ""),
    client_secret: if (client == null) "" else (client.secret or ""), note: note, notice: null}
