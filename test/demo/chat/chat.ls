// lambda demo chat: local participants use independent, real socket connections.
import dom
import chat: lambda.chat
import ui: .mod_ui
import config: .mod_config
import resources: ~~.mod_resources
let CSS = input(url_resolve(resources.entry_uri()^, "chat.css"), 'text')^
let ROOT = config.root
let REQUESTED_USER = config.setting("CHAT_DEMO_USER", chat.profiles[0].id)
let VALID_USER = chat.profile(REQUESTED_USER) != null
let DEFAULT_PEER = [for (p in chat.profiles where p.id != REQUESTED_USER) p.id][0]
let REQUESTED_PEER = config.setting("CHAT_DEMO_PEER", DEFAULT_PEER)
let VALID_PEER = chat.profile(REQUESTED_PEER) != null and REQUESTED_PEER != REQUESTED_USER

pn serve_demo(root) {
    chat.serve(0, ROOT ++ "/history.json", pn (e) => { report(root, e) }) ^ {
        report(root, {type: "error", message: ^.message})
    }
}
fn initial() => {user: if (VALID_USER) REQUESTED_USER else chat.profiles[0].id,
    peer: if (VALID_PEER) REQUESTED_PEER else DEFAULT_PEER, query: "", draft: "", inboxes: {},
    online: {}, endpoint: null, ready: false, shared: config.endpoint != null,
    note: if (not VALID_USER) "Unknown CHAT_DEMO_USER: " ++ REQUESTED_USER
        else if (not VALID_PEER) "Choose a different, valid CHAT_DEMO_PEER" else null}
pn report(root, event) {
    let target = dom.get_element_by_id(root, "chat")
    if (target != null) dom.dispatch(target, {type: "chat", bubbles: false, detail: event})
}
pn start_client(root, endpoint, user) {
    return start(chat.connect, [endpoint, user, pn (e) => { report(root, e) }], {owner: 'document'})
}
pn mark_read(st, writer) {
    if (writer == null) { return null }
    for (m in chat.conversation(st.inboxes[st.user] or [], st.user, st.peer) where m.to == st.user and m.status != "read") {
        send(writer, {type: "read", id: m.id, from: m.from}) ^ { null }
    }
}
pn submit(st, writers) {
    let text = trim(st.draft)
    if (text == "") { return {*: st, note: "Write a message first."} }
    if (len(text) > 4000) { return {*: st, note: "Please keep messages under 4000 characters."} }
    if (writers[st.user] == null or st.online[st.user] != true) { return {*: st, note: "Reconnect to send your message."} }
    let id = uuid()^
    let sent = send(writers[st.user], {type: "send", id: id, to: st.peer, text: text}) ^ { ^ }
    if (sent is error) { return {*: st, note: sent.message} }
    let queued = {id: id, seq: 0, from: st.user, to: st.peer, text: text,
        stamp: now().format("hh:mm"), status: "queued"}
    return {*: st, draft: "", note: null, inboxes: {*: st.inboxes, [st.user]: chat.merge(st.inboxes[st.user] or [], queued)}}
}
fn node(root, id) => dom.get_element_by_id(dom.root_node(root), id)
pn clear_draft(root) {
    let field = node(root, "draft")
    if (field != null) dom.set_state(field, "value", "")
}
pn scroll_messages(root) {
    let lines = dom.query_selector_all(dom.root_node(root), "#messages .message-line")
    if (len(lines) > 0) dom.scroll_into_view_options(lines[len(lines) - 1], {block: "end", inline: "nearest"})
}

view <chat_app> state st: initial(), server: null, clients: {}, writers: {} {
    <body ui.page(st)>
}
on load(evt) {
    if (server != null or st.ready) { return 'pass' }
    if (not VALID_USER or not VALID_PEER) { return 'handled' }
    let root = evt.target
    if (st.shared) {
        // one window owns one identity; it must not connect the other profiles
        // and displace their independent windows at the shared server.
        st = {*: st, endpoint: config.endpoint, ready: true}
        clients = {*: clients, [st.user]: start_client(root, config.endpoint, st.user)}
        return 'handled'
    }
    let made = io.mkdir(ROOT) ^ { ^ }
    if (made is error) { st = {*: st, note: made.message}; return 'handled' }
    server = start(serve_demo, [root], {owner: 'document'})
    return 'handled'
}
on chat(evt) {
    let e = evt.detail
    if (e.client != null and clients[e.user] != e.client) { return 'handled' }
    if (e.type == "listening") {
        st = {*: st, endpoint: e.endpoint, ready: true}
        let root = dom.root_node(evt.target)
        for (p in chat.profiles) { clients = {*: clients, [p.id]: start_client(root, e.endpoint, p.id)} }
    } else if (e.type == "connected") {
        writers = {*: writers, [e.user]: e.writer}
        st = {*: st, online: {*: st.online, [e.user]: true}, note: null}
    } else if (e.type == "offline") {
        writers = {*: writers, [e.user]: null}
        st = {*: st, online: {*: st.online, [e.user]: false}}
    } else if (e.type == "history") {
        var online = st.online
        if (e.user == st.user and e.online != null) {
            online = {}
            for (user in e.online) { online = {*: online, [user]: true} }
        }
        st = {*: st, online: online, inboxes: {*: st.inboxes, [e.user]: reduce([(st.inboxes[e.user] or []), *e.messages], (items, m) => chat.merge(items, m))}}
        if (e.user == st.user) mark_read(st, writers[st.user])
    } else if (e.type == "presence") {
        st = {*: st, online: {*: st.online, [e.peer]: e.online}}
    } else if (e.type == "message") {
        st = {*: st, inboxes: {*: st.inboxes, [e.user]: chat.merge(st.inboxes[e.user] or [], e.message)}}
        if (e.user == st.user and e.message.to == st.user and e.message.from == st.peer)
            mark_read(st, writers[st.user])
        scroll_messages(evt.target)
    } else if (e.type == "error") {
        st = {*: st, note: e.message}
    }
    return 'handled'
}
on click(evt) {
    let button = dom.closest(evt.target, "[data-action]")
    if (button == null) { return 'pass' }
    let action = dom.get_attribute(button, "data-action")
    if (action == "contact") {
        st = {*: st, peer: dom.get_attribute(button, "data-value"), draft: "", note: null}
        mark_read(st, writers[st.user])
    } else if (action == "send" or action == "hello") {
        let draft = if (action == "hello") "Hey " ++ split(chat.profile(st.peer).name, " ")[0] ++ ", how's your day going?" else dom.get_state(node(evt.target, "draft"), "value") or st.draft
        st = submit({*: st, draft: draft}, writers)
        if (st.draft == "") clear_draft(evt.target)
        scroll_messages(evt.target)
    } else if (action == "connection") {
        if (st.online[st.user] == true and writers[st.user] != null) {
            send(writers[st.user], {op: 'disconnect'}) ^ { null }
            st = {*: st, online: {*: st.online, [st.user]: false}}
        } else if (st.endpoint != null) {
            clients = {*: clients, [st.user]: start_client(dom.root_node(evt.target), st.endpoint, st.user)}
        }
    }
    return 'handled'
}
on input(evt) {
    let id = dom.get_attribute(evt.target, "id")
    if (id == "draft") { st = {*: st, draft: dom.get_state(evt.target, "value") or "", note: null} }
    else if (id == "search") { st = {*: st, query: dom.get_state(evt.target, "value") or ""} }
    else { return 'pass' }
    return 'handled'
}
on change(evt) {
    let id = dom.get_attribute(evt.target, "id")
    let value = dom.get_state(evt.target, "value")
    if (id == "profile") {
        if (st.shared) { return 'handled' }
        let old = st.user
        st = {*: st, user: value, peer: if (st.peer == value) old else st.peer, draft: "", note: null}
    } else if (id == "peer-select") { st = {*: st, peer: value, draft: "", note: null} }
    else { return 'pass' }
    clear_draft(evt.target)
    mark_read(st, writers[st.user])
    return 'handled'
}
on keydown(evt) {
    if (evt.key == "Enter" and not evt.shiftKey and dom.get_attribute(evt.target, "id") == "draft") {
        st = submit({*: st, draft: dom.get_state(evt.target, "value") or ""}, writers)
        if (st.draft == "") clear_draft(evt.target)
        return 'prevent-default'
    }
    return 'pass'
}
<html lang:"en", <head <meta charset:"UTF-8"><meta name:"viewport", content:"width=device-width, initial-scale=1">
    <title "Chat — Lambda"><style CSS>> apply(<chat_app>)>
