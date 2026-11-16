// Both carriers expose the same bounded binary messages to the existing peer protocol.
import ws: lambda.io.websocket
import rtc: lambda.io.rtc
import buffer: .peer_reader
pub fn channel(handle) => {transport:'rtc',handle:handle}
pub pn send(socket, bytes) any^ {
    if (socket.transport == 'buffered') { return send(socket.handle,bytes)^ }
    if (socket.transport == 'rtc') { return rtc.send(socket.handle,bytes)^ }
    return ws.send(socket,bytes)^
}
pub pn receive(socket, timeout) any^ {
    if (socket.transport == 'buffered') { return buffer.read(socket.reader,timeout)^ }
    if (socket.transport == 'rtc') { return rtc.receive(socket.handle,timeout)^ }
    return ws.receive(socket,timeout)^
}
pub pn close(socket) any^ {
    if (socket.transport == 'buffered') { return close(socket.handle)^ }
    if (socket.transport == 'rtc') { return rtc.close(socket.handle)^ }
    return ws.close(socket)^
}

pub let reader = buffer.run
pub pn ready(socket) any^ { return buffer.ready(socket.reader)^ }
pub pn finish(socket) any^ { return buffer.finish(socket.reader)^ }
