// an ordinary authenticated client stops after durable object/frame acknowledgments.
import agent: lambda.sync.peer_agent
import local: lambda.sync.local
import peer: lambda.sync.peer
import exchange: lambda.sync.peer_exchange
import session: lambda.sync.peer_session
import hex: lambda.security.tuf.metadata
import ws: lambda.io.websocket
pn main() {
    let environment = sys.proc.self.env#
    let request = input(environment.LAMBDA_SYNC_REQUEST,'json')^
    let current = agent.open_agent(request.directory,hex.unhex(environment.LAMBDA_SYNC_KEY)^)^
    local.scan(current.handle)^
    let outgoing = exchange.offer(current.handle,current.credentials)^
    if (len(outgoing.frames)^ < 2) raise error("interrupt fixture requires multiple frames")
    let socket = ws.open(request.url,{protocols:[session.protocol],max_message_bytes:session.max_frame})^
    var channel = peer.handshake(socket,current.credentials,'client',request.peer)^
    if (request.action == "hold") {
        output("authenticated",request.ready,{format:'text',atomic:true})^
        while (not exists(request.resume)) { sleep(20)^ }
        let response = peer.request(socket,channel,'head',{}) ^ { ^ }
        return response is error
    }
    let id = outgoing.ids[len(outgoing.ids)^-1]
    let encoded = exchange.object_text(current.handle,id)^
    var offset = 0
    while (offset < len(encoded)^) {
        let fragment = exchange.fragment(id,encoded,offset)^
        let received = peer.request(socket,channel,'put_object',fragment)^
        channel = received.session
        offset = offset+len(fragment.text)^
    }
    let received = peer.request(socket,channel,'put_frame',{packet:outgoing.frames[0],proof:outgoing.proof})^
    output("acknowledged",request.ready,{format:'text',atomic:true})^
    sleep(300000)^
}
