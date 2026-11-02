import ws: lambda.io.websocket
let ENV = sys.proc.self.env#
pn main() {
    let listener = ws.listen(0, {origins: ["http://allowed.test"], protocols: ["chat.v1"]})^
    output(string(listener.port), ENV.WEBSOCKET_TEST_PORT_FILE, {format: 'text'})^
    let socket = ws.accept(listener, 5000) ^ { ^ }
    if (socket is error) { return socket.code }
    let protocol = socket.protocol
    let event = ws.receive(socket, 5000)^
    ws.close(listener)^
    return [protocol, event.code, event.was_clean]
}
