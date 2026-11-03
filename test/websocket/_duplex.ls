import ws: lambda.io.websocket
pn bulk(socket, text) { ws.send(socket, text)^ }
pn main() {
    let socket = ws.open(sys.proc.self.env.WEBSOCKET_TEST_URL#, {max_message_bytes: 16777216})^
    var text = "x"
    for (i in 1 to 23) { text = text ++ text }
    let sending = start(bulk, [socket, text])
    let incoming = ws.receive(socket, 3000)^
    wait(sending)^
    ws.close(socket)^
    if (incoming.type == "close") { return [incoming.type, incoming.code, incoming.was_clean] }
    return [incoming.type, incoming.data]
}
