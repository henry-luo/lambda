import ws: lambda.io.websocket

pn echo(listener) {
    let socket = ws.accept(listener, 3000)^
    var kinds = []
    for (i in 1 to 3) {
        let event = ws.receive(socket, 3000)^
        kinds = [*kinds, event.type]
        ws.send(socket, event.data)^
    }
    let closed = ws.receive(socket, 3000)^
    return [kinds, closed.type, closed.code]
}
pn main() {
    let listener = ws.listen(0)^
    let server = start(echo, [listener])
    let socket = ws.open("ws://127.0.0.1:" ++ string(listener.port) ++ "/chat")^
    var replies = []
    for (payload in ["Hello 世界 👋", b'00FF7F', ""]) {
        ws.send(socket, payload)^
        let event = ws.receive(socket, 3000)^
        replies = [*replies, [event.type, event.data == payload]]
    }
    ws.close(socket)^
    let finished = wait(server)^
    ws.close(listener)^
    return [replies, finished]
}
