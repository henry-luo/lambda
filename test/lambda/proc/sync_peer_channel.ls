import identity: lambda.sync.peer_identity
import session: lambda.sync.peer_session
import peer: lambda.sync.peer
import crypto: lambda.io.crypto
import ws: lambda.io.websocket

pn server(listener, credentials) any^ {
    let socket = ws.accept(listener,5000)^
    let channel = peer.handshake(socket,credentials,'server')^
    let frame = ws.receive(socket,5000)^
    let received = session.unseal(channel,frame.data)^
    let reply = session.seal(received.session,{ok:true,body:received.value})^
    ws.send(socket,reply.bytes)^
    ws.close(socket)^
    return received.value
}
pn main() {
    let a = identity.create("channel",crypto.key()^)^
    let initial = identity.create("pending",crypto.key()^)^
    let bob = identity.enroll(initial,identity.invite(a,initial.device)^,a.device.id)^
    let alice = identity.trust(a,bob.device)^
    let listener = peer.listener(0)^
    let serving = start(server,[listener,bob])
    let socket = ws.open("ws://127.0.0.1:" ++ string(listener.port)^ ++ "/sync",
        {protocols:[session.protocol]})^
    let channel = peer.handshake(socket,alice,'client',bob.device.id,
        pn (id)=> { sleep(1)^; return identity.member(alice,id)^ })^
    let request = {kind:'test',body:{bytes:b'00ff',text:"retained across grant callback"}}
    let response = peer.request(socket,channel,request.kind,request.body)^
    ws.close(socket)^
    let received = wait(serving)^
    ws.close(listener)^
    return [response.value == request,received == request,response.session.sent == 1,
        response.session.received == 1]
}
