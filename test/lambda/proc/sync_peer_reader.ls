import wire: lambda.sync.peer_transport
import ws: lambda.io.websocket
pn serve(listener) any^ {
    let socket = ws.accept(listener,2000)^
    ws.send(socket,binary("first")^)^
    ws.send(socket,binary("second")^)^
    sleep(250)^
    ws.send(socket,binary("third")^)^
    let done = ws.receive(socket,2000)^
    ws.close(socket)^
    return done.data == binary("done")^
}
pn main() {
    let listener = ws.listen(0,{bind:"127.0.0.1",protocols:["sync-reader-test"]})^
    let server = start(serve,[listener])
    let raw = ws.open("ws://127.0.0.1:" ++ string(listener.port),{protocols:["sync-reader-test"]})^
    let reader = start(wire.reader,[raw])
    let socket = {transport:'buffered',handle:raw,reader:reader}
    let ready = wire.ready(socket)^
    let again = wire.ready(socket)^
    let first = wire.receive(socket,2000)^
    let second = wire.receive(socket,2000)^
    let timeout = wire.receive(socket,10) ^ { ^ }
    let cancelled = start(wire.ready,[socket])
    wait(cancelled,timeout:1) ^ { null }; cancel(cancelled); wait(cancelled) ^ { null }
    let third = wire.receive(socket,2000)^
    wire.send(socket,binary("done")^)^
    let served = wait(server)^
    ws.close(raw)^; wire.finish(socket)^; wait(reader)^; ws.close(listener)^
    return all([ready,again,first.data == binary("first")^,second.data == binary("second")^,
        timeout.code == 406,third.data == binary("third")^,served])
}
