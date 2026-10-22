// Run: ./lambda.exe demo mail
// A mail client in the spirit of macOS Mail (vibe/Lambda_Demo_Mail.md): IMAP
// accounts with app passwords or Proton Mail Bridge, a sidebar of mailboxes, a
// message list that mounts only the rows in view, and a viewer that shows HTML
// mail script-free in a sandboxed frame. The profile lives in
// ~/Library/Application Support/Lambda Mail (MAIL_DEMO_ROOT overrides it);
// passwords and refresh tokens stay in the Keychain. Sign-in with Google or
// Microsoft uses the app registration in the profile's oauth.mark (README).
import dom
import store: lambda.mail.store
import prov: lambda.mail.providers
import san: lambda.mail.sanitize
import secret: lambda.io.secret
import oauth: lambda.oauth
import resources: ~~.mod_resources
import model: .mod_model
import ui: .mod_ui
import sync: .mod_sync
import signin: .mod_signin

let BASE = url_resolve(resources.entry_uri()^, "./")
let CSS = input(url_resolve(BASE, "mail.css"), 'text')^
let CWD = sys.proc.self.cwd#
let HOME = sys.proc.self.env.HOME#
let ROOT_SETTING = sys.proc.self.env.MAIL_DEMO_ROOT#
let CUSTOM_ROOT = ROOT_SETTING != null and ROOT_SETTING != ""
let ROOT = if (not CUSTOM_ROOT) HOME ++ "/Library/Application Support/Lambda Mail"
    else if (starts_with(ROOT_SETTING, "/")) ROOT_SETTING else CWD ++ "/" ++ ROOT_SETTING
// another profile keeps its passwords apart from the default one's
let SERVICE = if (CUSTOM_ROOT) "Lambda Mail (" ++ ROOT ++ ")" else "Lambda Mail"
let DOWNLOADS = HOME ++ "/Downloads"

// An account task's report becomes `on mail(evt)` on the app: dom.dispatch
// from the task, looking the element up each time since a rebuild may
// replace it (AR-O13).
pn report(root, message) {
    let owner = dom.get_element_by_id(root, "mail")
    if (owner != null) dom.dispatch(owner, {type: "mail", bubbles: false, detail: message})
}

pn today() {
    let d = datetime()
    return {year: d.year, month: d.month, day: d.day}
}

// ---- the profile: accounts (never their passwords) and the stores ----

pn read_mark(path, fallback) {
    if (not exists(path)) { return fallback }
    var value = fallback
    input(path, 'mark') ^ { value = fallback } ~ { value = ~ }
    return value
}

pn read_accounts() {
    let saved = read_mark(ROOT ++ "/accounts.mark", null)
    return if (saved == null or saved.accounts == null) [] else saved.accounts
}

pn cached_entries(account_id, path) {
    let loaded = store.load_index(store.index_path(sync.account_dir(ROOT, account_id), model.box_id(path)))
    return if (loaded == null) [] else loaded.entries
}

// Shows a mailbox from the store at once and asks the account's task to
// bring it up to date (§4.8: open from disk, then reconcile).
pn show_box(st, tasks, account_id, path) {
    let task = tasks[account_id]
    if (task != null) { send(task, {op: 'select', path: path}) ^ { null } }
    return {*: st, account: account_id, box: path, entries: cached_entries(account_id, path), selected: null, fetching: null}
}

pn start_account(root, clients, account, password) {
    let client = model.oauth_client(clients, account.oauth)
    return start(sync.run, [account, ROOT, SERVICE, password, client, pn (m) => { report(root, m) }], {owner: 'document'})
}

// The browser sign-ins (mod_signin.ls), each in a task of its own: the refresh
// token goes straight to the Keychain, so the window hears only the outcome.
pn sign_in_task(root, account, client) {
    var made = null
    var failure = null
    signin.sign_in_account(SERVICE, account, client, {}) ^ { failure = ^ } ~ { made = ~ }
    report(root, if (failure is error) {type: "sign_in_failed", text: failure.message}
        else {type: "signed_in", account: made})
}

pn gmail_task(root, client, accounts) {
    var made = null
    var failure = null
    signin.sign_in_gmail(SERVICE, prov.GOOGLE, client, accounts, {}) ^ { failure = ^ } ~ { made = ~ }
    report(root, if (failure is error) {type: "sign_in_failed", text: failure.message}
        else {type: "signed_in", account: made.account})
}

// a removed account's password or refresh token leaves the Keychain too
pn forget(id) { secret.delete(SERVICE, id) ^ { null } }

pn lookup(root, email) {
    var found = null
    prov.autoconfig(email) ^ { found = null } ~ { found = ~ }
    report(root, {type: "autoconfig", email: email, found: found})
}

// The account joins the list, or replaces the one of the same id, and its
// task stores the password (if any) and logs in. Returns the next state and tasks.
pn finish_account(root, st, tasks, a, password) {
    let accounts = model.with_account(st.accounts, a)
    var failed = false
    io.mkdir(ROOT) ^ { null }
    output({version: 1, accounts: accounts}, ROOT ++ "/accounts.mark", {atomic: true}) ^ { failed = true }
    if (failed) { return {st: {*: st, setup: {*: st.setup, note: "Cannot save the account list in " ++ ROOT ++ "."}}, tasks: tasks} }
    // a replaced account's task logs in again with the new credentials
    if (tasks[a.id] != null) { cancel(tasks[a.id]) }
    let next_tasks = {*: tasks, [a.id]: start_account(root, st.clients, a, password)}
    let boxes = st.mailboxes[a.id] or []
    let next = {*: st, accounts: accounts, setup: null, mailboxes: {*: st.mailboxes, [a.id]: boxes}}
    return {st: show_box(next, next_tasks, a.id, model.inbox_path(boxes)), tasks: next_tasks}
}

// The account leaves the list and the Keychain; its downloaded mail stays,
// and adding the same address again picks it up.
pn remove_account(st, tasks, id) {
    let accounts = model.without_account(st.accounts, id)
    var failed = false
    output({version: 1, accounts: accounts}, ROOT ++ "/accounts.mark", {atomic: true}) ^ { failed = true }
    if (failed) { return {st: {*: st, confirm_remove: null, note: "Cannot save the account list in " ++ ROOT ++ "."}, tasks: tasks} }
    if (tasks[id] != null) { cancel(tasks[id]) }
    start(forget, [id], {owner: 'document'})
    let next_tasks = {*: tasks, [id]: null}
    let rest = {*: st, accounts: accounts, confirm_remove: null}
    if (st.account != id) { return {st: rest, tasks: next_tasks} }
    if (len(accounts) == 0) {
        return {st: {*: rest, account: null, box: null, entries: [], selected: null, fetching: null}, tasks: next_tasks}
    }
    return {st: show_box(rest, next_tasks, accounts[0].id, model.inbox_path(st.mailboxes[accounts[0].id])), tasks: next_tasks}
}

// The account sheet's Add: a password account is added at once; an OAuth
// one first signs in through the browser, and is added when that succeeds.
pn add_account(root, st, tasks) {
    if (st.setup.signing) { return {st: st, tasks: tasks} }
    let made = model.account_from_setup(st.setup, [for (a in st.accounts) a.id], st.clients)
    if (made.error != null) { return {st: {*: st, setup: {*: st.setup, note: made.error}}, tasks: tasks} }
    let a = made.account
    if (a.auth == "oauth") {
        let signer = start(sign_in_task, [root, a, model.oauth_client(st.clients, a.oauth)], {owner: 'document'})
        return {st: {*: st, setup: {*: st.setup, signing: true, signing_with: a.oauth.name, signer: signer, note: null}}, tasks: tasks}
    }
    return finish_account(root, st, tasks, a, st.setup.password)
}

// ---- the open message ----

pn open_message(e, file) {
    var message = null
    input(file, 'eml') ^ { message = null } ~ { message = ~ }
    return if (message == null) null else model.message_view(message, e, file)
}

fn safe_name(name) => replace(replace(if (name == null or trim(name) == "") "attachment" else trim(name), "/", "_"), ":", "_")

// Quick Look: images and text in the sheet, PDF in a frame over a copy
pn preview_of(a) {
    let name = safe_name(a.filename)
    let base = {name: name, type: a.type, attachment: a, saved: null}
    if (starts_with(a.type, "image/") and a.content is binary) { return {*: base, image: san.data_url(a)} }
    if (starts_with(a.type, "text/") and a.content is string) { return {*: base, text: a.content} }
    if (a.type == "application/pdf" and a.content is binary) {
        let dir = ROOT ++ "/.preview"
        io.mkdir(dir) ^ { null }
        var written = true
        output(a.content, dir ++ "/" ++ name) ^ { written = false }
        if (written) { return {*: base, file: "file://" ++ dir ++ "/" ++ name} }
    }
    return base
}

// saved only when asked, never over an existing file; the path, or null
pn save_attachment(a) {
    let name = safe_name(a.filename)
    let dot = [for (i, ch in split(name, "") where ch == "." and i > 0) i]
    let stem = if (len(dot) == 0) name else slice(name, 0, dot[len(dot) - 1])
    let ext = if (len(dot) == 0) "" else slice(name, dot[len(dot) - 1])
    var path = DOWNLOADS ++ "/" ++ name
    var n = 2
    while (exists(path)) {
        path = DOWNLOADS ++ "/" ++ stem ++ " " ++ string(n) ++ ext
        n = n + 1
    }
    var saved = true
    output(if (a.content is string) binary(a.content) else a.content, path) ^ { saved = false }
    return if (saved) path else null
}

// ---- the window ----

view <mail_app> state st: model.initial(), tasks: {}, shown: null {
    <body ui.page(st, shown)>
}

on load(evt) {
    // the message frame's load reaches this handler too: open the profile once
    if (st.loaded) { return 'pass' }
    let root = dom.root_node(evt.target)
    let accounts = read_accounts()
    // the app's own registrations with OAuth providers (MQ4); none is fine
    let clients = read_mark(ROOT ++ "/oauth.mark", null)
    var boxes = {}
    var started = {}
    for (a in accounts) {
        boxes = {*: boxes, [a.id]: read_mark(sync.account_dir(ROOT, a.id) ++ "/mailboxes.mark", [])}
        started = {*: started, [a.id]: start_account(root, clients, a, null)}
    }
    tasks = started
    st = {*: st, accounts: accounts, mailboxes: boxes, clients: clients, loaded: true, now: today(),
        setup: if (len(accounts) == 0) model.setup_initial() else null}
    if (len(accounts) > 0) { st = show_box(st, tasks, accounts[0].id, model.inbox_path(boxes[accounts[0].id])) }
    return 'handled'
}

on mail(evt) {
    let m = evt.detail
    let signing = st.setup != null and st.setup.signing
    if (m.type == "signed_in" and signing) {
        let next = finish_account(dom.root_node(evt.target), st, tasks, m.account, null)
        st = next.st
        tasks = next.tasks
        return 'handled'
    }
    if (m.type == "sign_in_failed" and signing) {
        st = {*: st, setup: {*: st.setup, signing: false, signer: null, note: m.text}}
        return 'handled'
    }
    let box_before = st.box
    st = {*: model.apply_report(st, m), now: today()}
    if (st.box != box_before and m.account == st.account) {
        // the shown mailbox is gone from the server's list: show the Inbox
        st = show_box(st, tasks, st.account, st.box)
        shown = null
    }
    if (m.type == "message" and m.account == st.account and m.uid == st.fetching) {
        let e = model.entry(st, m.uid)
        shown = if (e == null) null else open_message(e, m.file)
        st = {*: st, fetching: null}
    }
    return 'handled'
}

on click(evt) {
    let button = dom.closest(evt.target, "[data-action]")
    if (button == null) { return 'pass' }
    let name = dom.get_attribute(button, "data-action")
    let value = dom.get_attribute(button, "data-value")
    if (name == "mailbox") {
        st = show_box(st, tasks, dom.get_attribute(button, "data-account"), value)
        shown = null
    } else if (name == "message") {
        let e = model.entry(st, int(value))
        if (e != null) {
            let file = store.message_path(sync.account_dir(ROOT, st.account), e.file)
            if (exists(file)) {
                shown = open_message(e, file)
                st = {*: st, selected: e.uid, fetching: null}
                // a POP3 account keeps read state only in the store (ML9)
                if (model.is_pop3(model.current(st)) and model.is_unread(e)) {
                    send(tasks[st.account], {op: 'seen', uid: e.uid}) ^ { null }
                }
            } else {
                shown = null
                st = {*: st, selected: e.uid, fetching: e.uid}
                send(tasks[st.account], {op: 'fetch', path: st.box, entry: e}) ^ { st = {*: st, fetching: null, note: "The account is not running."} }
            }
        }
    } else if (name == "remote") {
        if (shown != null) { st = {*: st, remote: [*st.remote, shown.file]} }
    } else if (name == "get-mail") {
        if (st.account != null and tasks[st.account] != null) { send(tasks[st.account], {op: 'refresh'}) ^ { null } }
    } else if (name == "add-account") {
        st = {*: st, setup: model.setup_initial()}
    } else if (name == "setup-cancel") {
        // a sign-in still waiting for the browser ends with the sheet
        if (st.setup != null and st.setup.signer != null) { cancel(st.setup.signer) }
        st = {*: st, setup: null}
    } else if (name == "login-gmail") {
        // Gmail in one press: Google's page, then the address Google reports
        let form = if (st.setup == null) model.setup_initial() else st.setup
        let client = model.oauth_client(st.clients, prov.GOOGLE)
        let problem = if (client == null) null else model.google_client_error(client.id, client.secret)
        if (client == null or problem != null) {
            // no client yet, or a saved one Google would not know: the panel, to correct it
            st = {*: st, setup: model.google_setup_form(form, client, problem)}
        } else if (not form.signing) {
            let signer = start(gmail_task, [dom.root_node(evt.target), client, st.accounts], {owner: 'document'})
            st = {*: st, setup: {*: form, google_setup: false, signing: true, signing_with: "Google", signer: signer, note: null, notice: null}}
        }
    } else if (name == "google-change") {
        let form = if (st.setup == null) model.setup_initial() else st.setup
        st = {*: st, setup: model.google_setup_form(form, model.oauth_client(st.clients, prov.GOOGLE), null)}
    } else if (name == "google-console") {
        start(oauth.open_browser, ["https://console.cloud.google.com/auth/clients"], {owner: 'document'})
    } else if (name == "google-setup-back") {
        st = {*: st, setup: {*: st.setup, google_setup: false, note: null}}
    } else if (name == "google-setup-save") {
        let problem = model.google_client_error(st.setup.client_id, st.setup.client_secret)
        if (problem != null) {
            st = {*: st, setup: {*: st.setup, note: problem}}
        } else {
            // the app's registration with Google (MQ4), kept beside the accounts
            let clients = {*: (st.clients or {}), google: {client_id: trim(st.setup.client_id), client_secret: trim(st.setup.client_secret)}}
            var saved = true
            io.mkdir(ROOT) ^ { null }
            output(clients, ROOT ++ "/oauth.mark", {atomic: true}) ^ { saved = false }
            st = if (saved) {*: st, clients: clients, setup: {*: st.setup, google_setup: false, client_id: "", client_secret: "",
                    note: null, notice: "Google sign-in is set up. Press Login with Gmail."}}
                else {*: st, setup: {*: st.setup, note: "Cannot save " ++ ROOT ++ "/oauth.mark."}}
        }
    } else if (name == "remove-account") {
        st = {*: st, confirm_remove: value}
    } else if (name == "keep-account") {
        st = {*: st, confirm_remove: null}
    } else if (name == "remove-account-confirm") {
        let next = remove_account(st, tasks, st.confirm_remove)
        st = next.st
        tasks = next.tasks
        shown = if (st.account == null or st.selected == null) null else shown
    } else if (name == "setup-save") {
        let next = add_account(dom.root_node(evt.target), st, tasks)
        st = next.st
        tasks = next.tasks
    } else if (name == "preview") {
        if (shown != null) { st = {*: st, preview: preview_of(shown.attachments[int(value)])} }
    } else if (name == "preview-close") {
        st = {*: st, preview: null}
    } else if (name == "save-attachment") {
        if (st.preview != null) {
            let path = save_attachment(st.preview.attachment)
            st = {*: st, preview: {*: st.preview, saved: path or "nowhere: the file could not be written"}}
        }
    } else {
        return 'pass'
    }
    return 'handled'
}

// the account sheet's fields; the password stays in memory until Add
on input(evt) {
    let field = dom.get_attribute(evt.target, "data-field")
    if (field == null or st.setup == null) { return 'pass' }
    st = {*: st, setup: {*: st.setup, [field]: dom.get_state(evt.target, "value") or "", note: null}}
    return 'handled'
}

on change(evt) {
    let field = dom.get_attribute(evt.target, "data-field")
    if (field == null or st.setup == null) { return 'pass' }
    st = {*: st, setup: {*: st.setup, [field]: dom.get_state(evt.target, "value") or "", note: null}}
    let form = st.setup
    // an unknown domain is looked up in the ISPDB, off the window's turn
    if (field == "email" and form.provider == "auto" and prov.preset_for(form.email) == null and prov.domain_of(form.email) != "") {
        st = {*: st, setup: {*: form, looking: true, found: null}}
        start(lookup, [dom.root_node(evt.target), form.email], {owner: 'document'})
    }
    return 'handled'
}

<html lang:"en",
    <head <meta charset:"UTF-8"><title "Mail — Lambda"><style CSS>>
    apply(<mail_app>)
>
