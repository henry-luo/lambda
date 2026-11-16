// A single reader buffers one frame; readiness never consumes it.
import ws: lambda.io.websocket
import rtc: lambda.io.rtc

pn reader_request(reader, kind) any^ {
    send(reader,{kind:kind,reply:self()})^
    let reply = receive() ^ {
        send(reader,{kind:'forget',reply:self()}) ^ { null }
        raise ^
    }
    if (reply.value is error) raise reply.value
    return reply.value
}
pub pn ready(reader) any^ { return reader_request(reader,'ready')^ }
pub pn finish(reader) any^ { return reader_request(reader,'finish')^ }
pn raw_receive(socket) any^ {
    if (socket.transport == 'rtc') { return rtc.receive(socket.handle,60000)^ }
    return ws.receive(socket,60000)^
}
pn pump(socket, parent) any^ {
    while (true) {
        let frame = raw_receive(socket) ^ { ^ }
        if (frame is error and frame.code == 406) { continue }
        send(parent,{kind:'frame',value:frame})^
        // One unconsumed frame bounds memory and preserves transport backpressure.
        receive()^
    }
}
pub pn run(socket) any^ {
    let source = start(pump,[socket,self()])
    var buffered = null
    var present = false
    var readers = []
    var watchers = []
    var closers = []
    var closed = false
    while (true) {
        let message = receive()^
        if (message.kind == 'forget') {
            readers = [for (item in readers where item != message.reply) item]
            watchers = [for (item in watchers where item != message.reply) item]
            closers = [for (item in closers where item != message.reply) item]
        } else if (message.kind == 'frame') {
            buffered = message.value; present = true
            closed = buffered is error or buffered.type == "close"
        } else if (message.kind == 'ready') {
            watchers = [*watchers,message.reply]
        } else if (message.kind == 'read') {
            readers = [*readers,message.reply]
        } else if (message.kind == 'finish') {
            closers = [*closers,message.reply]
        }
        if (closed and len(closers)^ > 0) {
            for (closer in closers) { send(closer,{value:true}) ^ { null } }
            // The pump is parked on its acknowledgement after terminal IO; join it before returning.
            cancel(source); wait(source) ^ { null }
            return true
        }
        // Once the protocol is finished, drain queued data so the close frame can arrive.
        if (present and not closed and len(closers)^ > 0) {
            buffered = null; present = false; send(source,true)^
        }
        if (present) {
            for (watcher in watchers) { send(watcher,{value:true}) ^ { null } }
            watchers = []
            // A cancelled waiter cannot consume a frame; failed delivery retains it.
            var delivered = false
            while (len(readers)^ > 0 and not delivered) {
                let target = readers[0]; readers = slice(readers,1)^
                let sent = send(target,{value:buffered}) ^ { ^ }
                delivered = not (sent is error)
            }
            if (delivered and not closed) { buffered = null; present = false; send(source,true)^ }
        }
    }
}

pub pn read(reader, timeout) any^ {
    // Time out a non-consuming readiness subscription, never a frame handoff.
    let pending = start(reader_request,[reader,'ready'])
    wait(pending,timeout:timeout) ^ {
        let failure = ^
        cancel(pending); wait(pending) ^ { null }
        if (failure.code == 310) raise error({code:406,message:"sync transport: read timed out"})
        raise failure
    }
    return reader_request(reader,'read')^
}
