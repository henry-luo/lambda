import ws: lambda.io.websocket

pn echo(listener) {
    let socket = ws.accept(listener, 5000)^
    let message = ws.receive(socket, 5000)^
    ws.send(socket, message.data)^
    ws.receive(socket, 5000)^
    return len(message.data)
}
pn main() {
    let bad_zero = ws.listen(0, {max_message_bytes:0}) ^ { ^ }
    let bad_large = ws.listen(0, {max_message_bytes:16777217}) ^ { ^ }
    let listener = ws.listen(0, {max_message_bytes:2097152})^
    let server = start(echo, [listener])
    let socket = ws.open("ws://127.0.0.1:" ++ string(listener.port), {max_message_bytes:2097152})^
    let payload = join(fill(1100000, "a"), "")
    ws.send(socket, payload)^
    let reply = ws.receive(socket, 5000)^
    ws.close(socket)^
    let received = wait(server)^
    ws.close(listener)^
    return [bad_zero is error, bad_large is error, received, reply.data == payload]
}
