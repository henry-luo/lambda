// The mail demo's account task (vibe/Lambda_Demo_Mail.md §5.1, ML3): one per
// account, owned by the document (S13.3.1v2), so it outlives the handler that
// starts it and ends with the window. An IMAP account's task holds its
// lambda.io.mail session, keeps the shown mailbox's index current and
// downloads messages on request. A POP3 account's task downloads new
// messages into the local Inbox each round and keeps their read state (ML9).
// Both report to the window through `notify`.
//
// Requests: {op: 'select', path}, {op: 'fetch', path, entry}, {op: 'seen',
// uid}, {op: 'refresh'}, and {op: 'tick'} from its own ticker. Reports carry
// the account's id:
// {type: 'status', state, text}, {type: 'mailboxes', boxes},
// {type: 'index', path, entries}, {type: 'message', path, uid, file} and
// {type: 'failed', text}.
import wire: lambda.io.mail
import secret: lambda.io.secret
import prov: lambda.mail.providers
import sync: lambda.mail.sync
import store: lambda.mail.store
import pop3: lambda.mail.pop3
import oauth: lambda.oauth
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
    {user: account.user, token: tokens.access_token, *: (if (server.security == 'plain') {tls: false} else {})}
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
        } else if (m.op == 'stop') {
            running = false
        }
        // 'seen' waits for M2, where read state goes to the server with STORE
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
        } else if (m.op == 'stop') {
            running = false
        }
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
    if (model.is_pop3(account)) { serve_pop3(account, service, client, dir, notify)^ }
    else { serve_imap(account, service, client, dir, notify)^ }
    cancel(tick)
    null
}
