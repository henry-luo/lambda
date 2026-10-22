// The mail demo's account task (vibe/Lambda_Demo_Mail.md §5.1, ML3): one per
// account, owned by the document (S13.3.1v2), so it outlives the handler that
// starts it and ends with the window. It holds the account's lambda.io.mail
// session, keeps the shown mailbox's index current, downloads messages on
// request, and reports to the window through `notify`.
//
// Requests: {op: 'select', path}, {op: 'fetch', path, entry}, {op: 'refresh'},
// and {op: 'tick'} from its own ticker. Reports carry the account's id:
// {type: 'status', state, text}, {type: 'mailboxes', boxes},
// {type: 'index', path, entries}, {type: 'message', path, uid, file} and
// {type: 'failed', text}.
import wire: lambda.io.mail
import secret: lambda.io.secret
import prov: lambda.mail.providers
import sync: lambda.mail.sync
import store: lambda.mail.store
import model: .mod_model

// the shown mailbox is checked this often (IDLE push is M2)
let POLL_MS = 120000

pub fn account_dir(root, id) => root ++ "/" ++ id

pn say(account, notify, m) { notify({*: m, account: account.id}) }

pn ticker(owner) {
    while (true) {
        sleep(POLL_MS)^
        send(owner, {op: 'tick'})^
    }
}

// A logged-in session with the account's mailboxes, or null once the reason
// is reported. The mailbox list is cached for the next offline start.
pn connect(account, service, dir, notify) {
    say(account, notify, {type: "status", state: "connecting", text: null})
    var password = null
    var failure = null
    secret.get(service, account.id) ^ { failure = ^ } ~ { password = ~ }
    if (failure is error) {
        say(account, notify, {type: "status", state: "failed", text: "Cannot read the password: " ++ failure.message})
        return null
    }
    if (password == null) {
        say(account, notify, {type: "status", state: "failed", text: "No password is saved for this account."})
        return null
    }
    var session = null
    let options = prov.session_options(account.imap, account.user, password, account.pin)
    wire.open(prov.imap_url(account.imap), options) ^ { failure = ^ } ~ { session = ~ }
    var caps = []
    if (session != null) { sync.capabilities(session) ^ { failure = ^ } ~ { caps = ~ } }
    var boxes = []
    if (session != null and not (failure is error)) { sync.mailboxes(session) ^ { failure = ^ } ~ { boxes = ~ } }
    if (failure is error) {
        say(account, notify, {type: "status", state: "failed", text: failure.message})
        return null
    }
    // cached for the next offline start; a failure only costs that
    output(boxes, dir ++ "/mailboxes.mark", {atomic: true}) ^ { null }
    say(account, notify, {type: "mailboxes", boxes: boxes})
    return {session: session, gmail: "x-gm-ext-1" in caps, selected: null}
}

// Brings one mailbox's index up to date and reports it; the connection on
// success (now with that mailbox selected), else null.
pn refresh(c, account, dir, path, notify) {
    say(account, notify, {type: "status", state: "syncing", text: null})
    var index = null
    var failure = null
    sync.sync_mailbox(c.session, dir, model.box_id(path), path, c.gmail) ^ { failure = ^ } ~ { index = ~ }
    if (failure is error) {
        say(account, notify, {type: "status", state: "failed", text: failure.message})
        return null
    }
    say(account, notify, {type: "index", path: path, entries: index.entries})
    say(account, notify, {type: "status", state: "online", text: null})
    return {*: c, selected: path}
}

// Downloads one message into the store; the connection, else null.
pn download(c, account, dir, path, e, notify) {
    var failure = null
    if (c.selected != path) { wire.command(c.session, "SELECT", [path]) ^ { failure = ^ } }
    var file = null
    if (not (failure is error)) { sync.fetch_message(c.session, dir, e) ^ { failure = ^ } ~ { file = ~ } }
    if (failure is error) {
        say(account, notify, {type: "failed", text: "Cannot download the message: " ++ failure.message})
        return null
    }
    say(account, notify, {type: "message", path: path, uid: e.uid, file: file})
    return {*: c, selected: path}
}

// The task. A first `password` (from the account sheet) is stored before the
// first login; it is never written anywhere else (ML6).
pub pn run(account, root, service, password, notify) null^ {
    let dir = account_dir(root, account.id)
    io.mkdir(dir) ^ { null }
    if (password != null) {
        var stored = true
        secret.set(service, account.id, password) ^ { stored = false }
        if (not stored) { say(account, notify, {type: "status", state: "failed", text: "Cannot save the password."}) }
    }
    let tick = start(ticker, [self()])
    var c = null
    var shown = null
    var running = true
    while (running) {
        if (c == null) {
            c = connect(account, service, dir, notify)
            if (c != null and shown != null) { c = refresh(c, account, dir, shown, notify) }
        }
        let m = receive()^
        if (m.op == 'select') {
            shown = m.path
            if (c != null) { c = refresh(c, account, dir, shown, notify) }
        } else if (m.op == 'fetch') {
            if (c == null) { say(account, notify, {type: "failed", text: "The account is offline."}) }
            if (c != null) { c = download(c, account, dir, m.path, m.entry, notify) }
        } else if ((m.op == 'refresh' or m.op == 'tick') and c != null and shown != null) {
            c = refresh(c, account, dir, shown, notify)
        } else if (m.op == 'stop') {
            running = false
        }
    }
    cancel(tick)
    null
}
