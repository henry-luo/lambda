import ws: lambda.io.websocket
pn main() {
    let socket = ws.open(sys.proc.self.env.WEBSOCKET_TEST_URL#)^
    let timeout = ws.receive(socket, 15) ^ { ^ }
    let message = ws.receive(socket, 3000)^
    ws.close(socket)^
    return [timeout.code, message.type, message.data]
}
