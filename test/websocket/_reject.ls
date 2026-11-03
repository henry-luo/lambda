import ws: lambda.io.websocket
pn main() {
    let socket = ws.open(sys.proc.self.env.WEBSOCKET_TEST_URL#) ^ { ^ }
    if (socket is error) { return socket.code }
    let event = ws.receive(socket, 1000) ^ { ^ }
    return if (event is error) event.code else event.type
}
