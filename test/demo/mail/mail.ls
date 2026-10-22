// Run: ./lambda.exe demo mail
// A mail client in the spirit of macOS Mail (vibe/Lambda_Demo_Mail.md): IMAP
// accounts with app passwords or Proton Mail Bridge, a sidebar of mailboxes, a
// message list that mounts only the rows in view, and a viewer that shows HTML
// mail script-free in a sandboxed frame. The profile lives in
// ~/Library/Application Support/Lambda Mail (MAIL_DEMO_ROOT overrides it);
// passwords stay in the Keychain.
import dom
import store: lambda.mail.store
import prov: lambda.mail.providers
import san: lambda.mail.sanitize
import resources: ~~.mod_resources
import model: .mod_model
import ui: .mod_ui
import sync: .mod_sync

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

pn start_account(root, account, password) {
    return start(sync.run, [account, ROOT, SERVICE, password, pn (m) => { report(root, m) }], {owner: 'document'})
}

pn lookup(root, email) {
    var found = null
    prov.autoconfig(email) ^ { found = null } ~ { found = ~ }
    report(root, {type: "autoconfig", email: email, found: found})
}

// The account sheet's Add: the account joins the list, and its task stores
// the password and logs in. Returns the next state and tasks.
pn add_account(root, st, tasks) {
    let made = model.account_from_setup(st.setup, [for (a in st.accounts) a.id])
    if (made.error != null) { return {st: {*: st, setup: {*: st.setup, note: made.error}}, tasks: tasks} }
    let a = made.account
    let accounts = [*st.accounts, a]
    var failed = false
    io.mkdir(ROOT) ^ { null }
    output({version: 1, accounts: accounts}, ROOT ++ "/accounts.mark", {atomic: true}) ^ { failed = true }
    if (failed) { return {st: {*: st, setup: {*: st.setup, note: "Cannot save the account list in " ++ ROOT ++ "."}}, tasks: tasks} }
    let next_tasks = {*: tasks, [a.id]: start_account(root, a, st.setup.password)}
    let next = {*: st, accounts: accounts, setup: null, mailboxes: {*: st.mailboxes, [a.id]: []}}
    return {st: show_box(next, next_tasks, a.id, "INBOX"), tasks: next_tasks}
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
    var boxes = {}
    var started = {}
    for (a in accounts) {
        boxes = {*: boxes, [a.id]: read_mark(sync.account_dir(ROOT, a.id) ++ "/mailboxes.mark", [])}
        started = {*: started, [a.id]: start_account(root, a, null)}
    }
    tasks = started
    st = {*: st, accounts: accounts, mailboxes: boxes, loaded: true, now: today(),
        setup: if (len(accounts) == 0) model.setup_initial() else null}
    if (len(accounts) > 0) { st = show_box(st, tasks, accounts[0].id, model.inbox_path(boxes[accounts[0].id])) }
    return 'handled'
}

on mail(evt) {
    let m = evt.detail
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
        st = {*: st, setup: null}
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
