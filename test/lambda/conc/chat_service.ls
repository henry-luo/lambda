import ws: lambda.io.websocket
import chat: lambda.chat

pn event(socket, kind) {
    while (true) {
        let frame = ws.receive(socket, 5000)^
        let value = parse(frame.data, 'json')^
        if (value.type == kind) { return value }
    }
}
pn main() {
    let root = "temp/chat-test-" ++ uuid()^
    io.mkdir(root)^
    let path = root ++ "/history.json"
    let owner = self()
    let server = start(chat.serve, [0, path, pn (m) => { send(owner, m)^ }])
    let ready = receive()^
    // acquire sockets in this task: they outlive the helper's return.
    let alice = ws.open(ready.endpoint)^
    let bob = ws.open(ready.endpoint)^
    ws.send(alice, format({type: "hello", user: "alex"}, 'json'))^
    let ah = event(alice, "history")
    ws.send(bob, format({type: "hello", user: "maya"}, 'json'))^
    let bh = event(bob, "history")
    let request = {type: "send", id: "one", from: "noah", to: "maya", text: "Hello 世界"}
    ws.send(alice, format(request, 'json'))^
    let accepted = event(alice, "message")
    let received = event(bob, "message")
    ws.send(bob, format({type: "received", id: "one", from: "alex"}, 'json'))^
    let delivered = event(alice, "message")
    ws.send(bob, format({type: "read", id: "one", from: "alex"}, 'json'))^
    let read = event(alice, "message")
    ws.send(alice, format(request, 'json'))^
    let duplicate = event(alice, "message")
    ws.close(bob)^
    ws.send(alice, format({type: "send", id: "two", to: "maya", text: "While you were offline"}, 'json'))^
    let offline = event(alice, "message")
    let saved = input(path, 'json')^
    ws.close(alice)^
    cancel(server)
    let stopped = wait(server) ^ { ^ }
    let restarted = start(chat.serve, [0, path, pn (m) => { send(owner, m)^ }])
    let ready2 = receive()^
    let live = ws.open(ready2.endpoint)^
    ws.send(live, format({type: "hello", user: "alex"}, 'json'))^
    let live_history = event(live, "history")
    let replay = ws.open(ready2.endpoint)^
    ws.send(replay, format({type: "hello", user: "maya"}, 'json'))^
    let history = event(replay, "history")
    ws.close(replay)^
    var reconnects = 0
    for (i in 1 to 12) {
        let again = ws.open(ready2.endpoint)^
        ws.send(again, format({type: "hello", user: "maya"}, 'json'))^
        let replayed = event(again, "history")
        if ((len(replayed.messages) == 2)^) { reconnects = reconnects + 1 }
        ws.close(again)^
    }
    ws.close(live)^
    cancel(restarted)
    let stopped2 = wait(restarted) ^ { ^ }
    io.delete(root)^
    return [len(ah.messages), len(bh.messages), accepted.message.from, received.message.text,
        accepted.message.status, delivered.message.status, read.message.status,
        duplicate.message.seq == accepted.message.seq, len(saved.messages), len(history.messages),
        history.messages[0].status, history.messages[1].text, reconnects]
}
