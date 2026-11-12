// Both carriers expose the same bounded binary messages to the existing peer protocol.
import ws: lambda.io.websocket
import rtc: lambda.io.rtc
pub fn channel(handle) => {transport:'rtc',handle:handle}
pub pn send(socket, bytes) any^ {
    if (socket.transport == 'rtc') { return rtc.send(socket.handle,bytes)^ }
    return ws.send(socket,bytes)^
}
pub pn receive(socket, timeout) any^ {
    if (socket.transport == 'rtc') { return rtc.receive(socket.handle,timeout)^ }
    return ws.receive(socket,timeout)^
}
pub pn close(socket) any^ {
    if (socket.transport == 'rtc') { return rtc.close(socket.handle)^ }
    return ws.close(socket)^
}
