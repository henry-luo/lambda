// The mail demo's account task (vibe/Lambda_Demo_Mail.md §5.1, ML3): one per
// account, owned by the document (S13.3.1v2), so it outlives the handler that
// starts it and ends with the window. An IMAP account's task holds its
// lambda.io.mail session, keeps the shown mailbox's index current and
// downloads messages on request. A POP3 account's task downloads new
// messages into the local Inbox each round and keeps their read state (ML9).
// Both report to the window through `notify`.
//
// The demo account's task serves a seeded local store (mod_demo.ls) and never
// connects.
//
// Requests: {op: 'select', path}, {op: 'fetch', path, entry}, {op: 'seen',
// path, uid}, {op: 'refresh'}, {op: 'create', path} for a new mailbox,
// {op: 'send', text, recipients, ...} and {op: 'tick'} from its own ticker.
// The demo account also takes {op: 'flag', path, uid, flag, enabled}, {op: 'move',
// path, uid, to}, {op: 'erase', path, uid} and {op: 'save_draft', path, text,
// replaces}. Reports carry
// the account's id:
// {type: 'status', state, text}, {type: 'mailboxes', boxes},
// {type: 'index', path, entries}, {type: 'message', path, uid, file},
// {type: 'sent'}, {type: 'send_failed', text}, {type: 'draft_saved'} and
// {type: 'failed', text}.
import wire: lambda.io.mail
import secret: lambda.io.secret
import prov: lambda.mail.providers
import sync: lambda.mail.sync
import store: lambda.mail.store
import pop3: lambda.mail.pop3
import oauth: lambda.oauth
import model: .mod_model
import demo: .mod_demo

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

// What lambda.io.mail open takes to log in to `server`: the saved password,
// or for an OAuth account a fresh access token from its saved refresh token
// (M4). `client` is the app's registration with the account's provider.
pn login_options(account, server, service, client) map^ {
    let saved = secret.get(service, account.id)^
    let signed_in = account.auth == "oauth"
    if (saved == null) {
        raise error(if (signed_in) "Sign in again: no refresh token is saved." else "No password is saved for this account.")
    }
    if (not signed_in) { return prov.session_options(server, account.user, saved, account.pin) }
    if (client == null) { raise error("No " ++ account.oauth.name ++ " client is configured; see the mail demo's README.") }
    let tokens = oauth.refresh(account.oauth, client, saved)^
    // a provider that rotates refresh tokens (Microsoft) hands out a new one
    if (tokens.refresh_token != null and tokens.refresh_token != saved) { secret.set(service, account.id, tokens.refresh_token)^ }
    {user: account.user, token: tokens.access_token, *: prov.tls_option(server)}
}

// A logged-in session with the account's mailboxes, or null once the reason
// is reported. The mailbox list is cached for the next offline start.
pn connect(account, service, client, dir, notify) {
    say(account, notify, {type: "status", state: "connecting", text: null})
    var options = null
    var failure = null
    login_options(account, account.imap, service, client) ^ { failure = ^ } ~ { options = ~ }
    if (failure is error) {
        say(account, notify, {type: "status", state: "failed", text: failure.message})
        return null
    }
    var session = null
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

// Sends a message through the account's SMTP server (§4.6); the window hears
// "sent" or "send_failed". Sent copies are not appended yet (P2).
pn smtp_send(account, service, client, m, notify) {
    var failure = null
    var options = null
    if (account.smtp == null) { failure = error("This account has no outgoing (SMTP) server.") }
    if (account.smtp != null) { login_options(account, account.smtp, service, client) ^ { failure = ^ } ~ { options = ~ } }
    if (not (failure is error)) {
        wire.send(prov.smtp_url(account.smtp), {*: options, from: account.email, to: m.recipients}, m.text) ^ { failure = ^ }
    }
    say(account, notify, if (failure is error) {type: "send_failed", text: failure.message} else {type: "sent"})
}

// A new mailbox on the server, then the refreshed mailbox list.
pn create_mailbox(c, account, dir, path, notify) {
    var failure = null
    var boxes = null
    wire.command(c.session, "CREATE", [path]) ^ { failure = ^ }
    if (not (failure is error)) { sync.mailboxes(c.session) ^ { failure = ^ } ~ { boxes = ~ } }
    if (failure is error) {
        say(account, notify, {type: "failed", text: "Cannot create the mailbox: " ++ failure.message})
    } else {
        output(boxes, dir ++ "/mailboxes.mark", {atomic: true}) ^ { null }
        say(account, notify, {type: "mailboxes", boxes: boxes})
    }
}

pn serve_imap(account, service, client, dir, notify) null^ {
    var c = null
    var shown = null
    var running = true
    while (running) {
        if (c == null) {
            c = connect(account, service, client, dir, notify)
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
        } else if (m.op == 'create') {
            if (c == null) { say(account, notify, {type: "failed", text: "The account is offline."}) }
            else { create_mailbox(c, account, dir, m.path, notify) }
        } else if (m.op == 'send') {
            smtp_send(account, service, client, m, notify)
        } else if (m.op == 'stop') {
            running = false
        }
        // 'seen', flags and moves wait for M2, where they go to the server with STORE and MOVE
    }
    null
}

// One POP3 round in a task of its own: its session closes when the task ends
// (ML3), which releases the server's maildrop lock (RFC 1939 §8).
pn pop3_round(account, service, client, dir) map^ {
    let session = wire.open(prov.pop3_url(account.pop3), login_options(account, account.pop3, service, client)^)^
    pop3.sync_inbox(session, dir, null)
}

pn pop3_refresh(account, service, client, dir, notify) {
    say(account, notify, {type: "status", state: "syncing", text: null})
    let outcome: map | error = wait(start(pop3_round, [account, service, client, dir]))
    if (outcome is error) {
        say(account, notify, {type: "status", state: "failed", text: outcome.message})
    } else {
        say(account, notify, {type: "index", path: pop3.INBOX, entries: outcome.entries})
        say(account, notify, {type: "status", state: "online", text: null})
    }
}

// A POP3 account's local Inbox: every message is on disk once synced, so
// there is nothing to fetch on demand.
pn serve_pop3(account, service, client, dir, notify) null^ {
    say(account, notify, {type: "mailboxes", boxes: pop3.mailboxes()})
    pop3_refresh(account, service, client, dir, notify)
    var running = true
    while (running) {
        let m = receive()^
        if (m.op == 'select' or m.op == 'refresh' or m.op == 'tick') {
            pop3_refresh(account, service, client, dir, notify)
        } else if (m.op == 'seen') {
            var index = null
            pop3.mark_seen(dir, m.uid, true) ^ { say(account, notify, {type: "failed", text: "Cannot save the read state."}) } ~ { index = ~ }
            if (index != null) { say(account, notify, {type: "index", path: pop3.INBOX, entries: index.entries}) }
        } else if (m.op == 'fetch') {
            say(account, notify, {type: "failed", text: "The message file is missing from the store."})
        } else if (m.op == 'send') {
            smtp_send(account, service, client, m, notify)
        } else if (m.op == 'create') {
            say(account, notify, {type: "failed", text: "A POP3 account has only its Inbox."})
        } else if (m.op == 'stop') {
            running = false
        }
    }
    null
}

pn reported(account, notify, indexes) {
    for (ix in indexes) { if (ix != null) { say(account, notify, {type: "index", path: ix.path, entries: ix.entries}) } }
}

// One request to the demo account; raises when its store cannot be written.
pn demo_request(account, dir, m, notify) null^ {
    if (m.op == 'select') { reported(account, notify, [demo.load(dir, m.path)]) }
    else if (m.op == 'refresh') { reported(account, notify, [demo.deliver(dir)^]) }
    else if (m.op == 'seen') { reported(account, notify, [demo.set_flag(dir, m.path, m.uid, "\\Seen", true)^]) }
    else if (m.op == 'flag') { reported(account, notify, [demo.set_flag(dir, m.path, m.uid, m.flag, m.enabled)^]) }
    else if (m.op == 'move') { reported(account, notify, demo.move(dir, m.path, m.uid, m.to)^ or []) }
    else if (m.op == 'erase') { reported(account, notify, [demo.erase(dir, m.path, m.uid)^]) }
    else if (m.op == 'create') {
        say(account, notify, {type: "mailboxes", boxes: demo.create_mailbox(dir, m.path)^})
        reported(account, notify, [demo.load(dir, m.path)])
    } else if (m.op == 'send' or m.op == 'save_draft') {
        // sent mail is filed in Sent, a draft in Drafts (replacing the one it was opened from)
        let sending = m.op == 'send'
        let filed = demo.file_into(dir, m.path, m.text, if (sending) ["\\Seen"] else ["\\Seen", "\\Draft"])^
        let arrived = if (sending and m.to_self) demo.file_into(dir, "INBOX", m.text, [])^ else null
        let left = if (m.replaces != null) demo.erase(dir, m.replaces.path, m.replaces.uid)^ else null
        reported(account, notify, [filed, arrived, left])
        say(account, notify, {type: if (sending) "sent" else "draft_saved"})
    } else if (m.op == 'fetch') {
        say(account, notify, {type: "failed", text: "The message file is missing from the demo mailbox."})
    }
    null
}

// The demo account: every mailbox is on disk from the start, so the sidebar
// gets all its unread counts at once. Get Mail delivers the next arrival.
pn serve_demo(account, dir, notify) null^ {
    if (not demo.is_seeded(dir)) {
        say(account, notify, {type: "status", state: "syncing", text: "Preparing the demo mailbox…"})
        demo.seed(dir)^
    }
    let boxes = demo.mailboxes(dir)
    say(account, notify, {type: "mailboxes", boxes: boxes})
    reported(account, notify, [for (b in boxes) demo.load(dir, b.path)])
    say(account, notify, {type: "status", state: "online", text: null})
    var running = true
    while (running) {
        let m = receive()^
        if (m.op == 'stop') { running = false }
        if (m.op != 'stop') { demo_request(account, dir, m, notify) ^ { say(account, notify, {type: "failed", text: "The demo mailbox: " ++ ^.message}) } }
    }
    null
}

// The task. A first `password` (from the account sheet) is stored before the
// first login; it is never written anywhere else (ML6). An OAuth account's
// refresh token was stored by its sign-in; `client` is the app's
// registration with that account's provider, else null.
pub pn run(account, root, service, password, client, notify) null^ {
    let dir = account_dir(root, account.id)
    io.mkdir(dir) ^ { null }
    if (password != null) {
        var stored = true
        secret.set(service, account.id, password) ^ { stored = false }
        if (not stored) { say(account, notify, {type: "status", state: "failed", text: "Cannot save the password."}) }
    }
    let tick = start(ticker, [self()])
    if (model.is_demo(account)) { serve_demo(account, dir, notify)^ }
    else if (model.is_pop3(account)) { serve_pop3(account, service, client, dir, notify)^ }
    else { serve_imap(account, service, client, dir, notify)^ }
    cancel(tick)
    null
}
