import ws: lambda.io.websocket
pn receiver(listener, owner) {
    let socket = ws.accept(listener, 3000)^
    send(owner, "ready")^
    let outcome = ws.receive(socket) ^ { ^ }
    return outcome.code
}
pn main() {
    let listener = ws.listen(0)^
    let task = start(receiver, [listener, self()])
    let socket = ws.open("ws://127.0.0.1:" ++ string(listener.port) ++ "/chat")^
    receive()^
    cancel(task)
    let result = wait(task)^
    ws.close(socket) ^ { null }
    ws.close(listener)^
    return result
}
