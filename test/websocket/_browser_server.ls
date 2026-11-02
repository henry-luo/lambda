import ws: lambda.io.websocket
let ENV = sys.proc.self.env#
pn main() {
    let listener = ws.listen(0, {origins: [ENV.WEBSOCKET_TEST_ORIGIN], protocols: ["chat.v1"]})^
    output(string(listener.port), ENV.WEBSOCKET_TEST_PORT_FILE, {format: 'text'})^
    let socket = ws.accept(listener, 10000)^
    for (i in 1 to 3) {
        let event = ws.receive(socket, 5000)^
        ws.send(socket, event.data)^
    }
    let closed = ws.receive(socket, 5000)^
    ws.close(listener)^
    return [closed.code, closed.data, closed.was_clean]
}
