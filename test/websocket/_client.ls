import ws: lambda.io.websocket
let ENV = sys.proc.self.env#
pn main() {
    let socket = ws.open(ENV.WEBSOCKET_TEST_URL, {ca_file: ENV.WEBSOCKET_TEST_CA, max_message_bytes: 1048576})^
    ws.send(socket, "Hello 世界")^
    let text = ws.receive(socket, 3000)^
    let payload = ws.receive(socket, 3000)^
    let closed = ws.receive(socket, 3000)^
    return [text.type, text.data, payload.type, payload.data == b'00FF', closed.type, closed.code]
}
