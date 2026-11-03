// Local demonstration messaging over real WebSockets. D7.2.4v4, S13.1–S13.3.
// Demo profiles are identities, not authentication credentials.
import ws: lambda.io.websocket
import model: .chat.model
import attachments: .chat.attachment

pub let profiles = model.PROFILES
pub fn profile(id) => model.profile(id)
pub fn conversation(messages, user, peer) => model.conversation(messages, user, peer)
pub fn merge(messages, message) => model.merge(messages, message)
pub fn unread(messages, user, peer) => model.unread(messages, user, peer)
pub fn preview(messages, user, peer) => model.preview(messages, user, peer)

pn writer(socket) {
    while (true) {
        let m = receive()^
        if (m.op == 'disconnect') { ws.close(socket)^; return null }
        ws.send(socket, format(m, 'json'))^
    }
}
pn report(notify, m) { if (notify != null) notify(m) }
pn enqueue(outbox, event) { if (outbox != null) send(outbox, event)^ }

pn peer(socket, service) {
    let outbox = start(writer, [socket])
    var user = null
    while (true) {
        let frame = ws.receive(socket) ^ { ^ }
        if (frame is error or frame.type == "close") { break }
        if (frame.type != "text") { continue }
        let m = parse(frame.data, 'json') ^ { ^ }
        if (m is error) { enqueue(outbox, {type: "error", message: "Invalid JSON."}); continue }
        if (user == null) {
            if (m.type != "hello" or model.profile(m.user) == null) {
                enqueue(outbox, {type: "error", message: "Choose a demo profile first."})
                continue
            }
            user = m.user
            send(service, {op: 'join', user: user, writer: outbox})^
            continue
        }
        send(service, {op: 'request', user: user, writer: outbox, request: m})^
    }
    ws.close(socket) ^ { null }
    cancel(outbox)
    if (user != null) send(service, {op: 'leave', user: user, writer: outbox}) ^ { null }
}
pn peer_worker(service, dispatcher) {
    while (true) {
        send(dispatcher, {worker: self()})^
        let request = receive()^
        peer(request.socket, service)
    }
}
pn accept_clients(listener, service) {
    // only idle workers receive connections; a busy participant cannot stall a reconnect.
    let workers = [for (i in 1 to 8) start(peer_worker, [service, self()])]
    while (true) {
        let available = receive()^
        var socket = null
        while (socket == null) {
            let accepted = ws.accept(listener) ^ { ^ }
            if (accepted is error) { sleep(10)^; continue }
            socket = accepted
        }
        send(available.worker, {socket: socket})^
    }
}
pn broadcast(peers, event) {
    for (p in model.PROFILES) {
        if (peers[p.id] != null) enqueue(peers[p.id], event)
    }
}
pn persist_history(path, messages) { output({version: 1, messages: messages}, path, {format: 'json', atomic: true})^ }

// one task owns snapshots and membership; an echoed message means saved.
pub pn serve(port, path, notify) {
    var messages = []
    if (exists(path)) {
        let saved = input(path, 'json')^
        if (saved.version != 1 or not (saved.messages is array)) { raise error("Chat: unsupported history file") }
        messages = saved.messages
    }
    let listener = ws.listen(port, {max_message_bytes:attachments.FRAME_BYTES})^
    let acceptor = start(accept_clients, [listener, self()])
    var peers = {}
    report(notify, {type: "listening", endpoint: "ws://127.0.0.1:" ++ string(listener.port) ++ "/chat"})
    while (true) {
        let m = receive()^
        if (m.op == 'join') {
            if (peers[m.user] != null) enqueue(peers[m.user], {op: 'disconnect'})
            peers = {*: peers, [m.user]: m.writer}
            // a new client needs existing membership before later presence deltas.
            let batches = model.history_batches(model.for_user(messages, m.user))^
            for (i, batch in batches) enqueue(m.writer, {type: "history", messages:batch,
                online:if (i == 0) [for (p in model.PROFILES where peers[p.id] != null) p.id] else null})
            broadcast(peers, {type: "presence", peer: m.user, online: true})
            continue
        }
        if (m.op == 'leave') {
            // an old connection cannot remove a replacement for the same identity.
            if (peers[m.user] == m.writer) {
                peers = {*: peers, [m.user]: null}
                broadcast(peers, {type: "presence", peer: m.user, online: false})
            }
            continue
        }
        if (m.op != 'request' or peers[m.user] != m.writer) { continue }
        let r = m.request
        if (r.type == "send") {
            let next = model.accept(messages, m.user, r, now().format("hh:mm"))
            if (next.error != null) { enqueue(m.writer, {type: "error", message: next.error}); continue }
            let stored = persist_history(path, next.messages) ^ { ^ }
            if (stored is error) { enqueue(m.writer, {type: "error", message: "Could not save the message: " ++ stored.message}); continue }
            messages = next.messages
            enqueue(m.writer, {type: "message", message: next.message})
            enqueue(peers[next.message.to], {type: "message", message: next.message})
        }
        if (r.type == "received" or r.type == "read") {
            let next = model.receipt(messages, m.user, r.id, r.from, if (r.type == "read") "read" else "delivered")
            if (next == messages) { continue }
            let stored = persist_history(path, next) ^ { ^ }
            if (stored is error) { enqueue(m.writer, {type: "error", message: "Could not save a receipt."}); continue }
            messages = next
            let changed = model.message_of(messages, r.from, r.id)
            enqueue(m.writer, {type: "message", message: changed})
            enqueue(peers[r.from], {type: "message", message: changed})
        }
    }
}

// a reader and mailbox-driven writer share a task-owned connection.
pub pn connect(endpoint, user, notify) {
    let socket = ws.open(endpoint, {max_message_bytes:attachments.FRAME_BYTES}) ^ { ^ }
    if (socket is error) { report(notify, {type: "offline", user: user, client: self(), message: socket.message}); return null }
    let outbox = start(writer, [socket])
    enqueue(outbox, {type: "hello", user: user})
    report(notify, {type: "connected", user: user, client: self(), writer: outbox})
    while (true) {
        let frame = ws.receive(socket) ^ { ^ }
        if (frame is error or frame.type == "close") { break }
        if (frame.type != "text") { continue }
        let event = parse(frame.data, 'json') ^ { ^ }
        if (event is error) { report(notify, {type: "error", user: user, client: self(), message: "The server sent invalid JSON."}); continue }
        if (event.type == "message" and event.message.to == user and event.message.status == "sent")
            enqueue(outbox, {type: "received", id: event.message.id, from: event.message.from})
        if (event.type == "history") {
            for (m in event.messages where m.to == user and m.status == "sent")
                enqueue(outbox, {type: "received", id: m.id, from: m.from})
        }
        report(notify, {*: event, user: user, client: self()})
    }
    ws.close(socket) ^ { null }
    cancel(outbox)
    report(notify, {type: "offline", user: user, client: self(), message: "Disconnected"})
}
