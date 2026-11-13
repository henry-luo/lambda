// A short-lived authenticated channel carries several pinned exchange rounds.
import peer: .peer
import session: .peer_session
import exchange: .peer_exchange
import agent: .peer_agent
import ws: lambda.io.websocket
import timing: .timing

pn notify(report, event) any^ { if (report != null) report(event)^; return true }
pn finished(handle, report, round, count) any^ {
    agent.materialize(handle)^
    let installed = agent.resume_installation(handle)^.state
    notify(report,{type:'round',peer:round.result.peer,token:round.token,progress:round.result,rounds:count,
        installation:installed.installation != null,review:installed.review != null,
        warning:if (installed.installation == null) null else installed.installation.blocked.reason})^
    return true
}
pub pn client(socket, handle, credentials, expected, authorize, options, report=null) any^ {
    var channel = peer.handshake(socket,credentials,'client',expected,authorize,session.burst_protocol)^
    notify(report,{type:'connected',peer:channel.peer.id})^
    let started = timing.milliseconds()
    var last_activity = started
    var token = null
    var needed = true
    var rounds = 0
    while (timing.milliseconds()-started < options.burst_ms and rounds < 1024) {
        if (needed or token != exchange.current_token(handle)^) {
            let round = peer.exchange_round(socket,channel,handle,credentials)^
            channel = round.session; token = round.token; rounds = rounds+1
            finished(handle,report,round,rounds)^
            last_activity = timing.milliseconds(); needed = false
        }
        if (timing.milliseconds()-last_activity >= options.idle_ms) { break }
        let incoming = peer.read_record(socket,channel,options.interval_ms) ^ { ^ }
        if (incoming is error) {
            if (incoming.code != 406) raise incoming
            continue
        }
        {
            channel = (incoming^).session
            if ((incoming^).value.kind != 'changed') raise error("sync burst: unexpected idle record")
            let digest = (incoming^).value.body
            if (not (digest is binary) or len(digest)^ != 32) raise error("sync burst: invalid change fingerprint")
            needed = digest != exchange.current_token(handle)^.digest
        }
    }
    peer.request(socket,channel,'close',null)^
    return {peer:expected,token:token,rounds:rounds}
}
pub pn server(socket, handle, credentials, authorize, options, report=null) any^ {
    var channel = peer.handshake(socket,credentials,'server',null,authorize,session.burst_protocol)^
    notify(report,{type:'connected',peer:channel.peer.id})^
    let started = timing.milliseconds()
    var last_activity = started
    var announced = exchange.current_token(handle)^
    var delivered = null
    var rounds = 0
    while (timing.milliseconds()-started < options.burst_ms+1000 and rounds < 1024) {
        let incoming = peer.read_record(socket,channel,options.interval_ms) ^ { ^ }
        if (incoming is error) {
            if (incoming.code != 406) raise incoming
            let current = exchange.current_token(handle)^
            if (current != announced) {
                channel = peer.send_record(socket,channel,{kind:'changed',body:current.digest})^
                announced = current
            }
            if (timing.milliseconds()-last_activity >= options.idle_ms+1000) { break }
            continue
        }
        channel = (incoming^).session
        if ((incoming^).value == {kind:'close',body:null}) {
            peer.send_record(socket,channel,{ok:true,body:true})^
            return {peer:channel.peer.id,token:delivered,rounds:rounds}
        }
        if ((incoming^).value != {kind:'head',body:{}}) raise error("sync burst: expected a new round")
        let round = peer.serve_round(socket,channel,handle,credentials,null,incoming^)^
        channel = round.session; delivered = round.token; announced = round.token; rounds = rounds+1
        finished(handle,report,round,rounds)^
        last_activity = timing.milliseconds()
    }
    return {peer:channel.peer.id,token:delivered,rounds:rounds}
}
pub pn connect(url, handle, credentials, expected, authorize, options, report=null) any^ {
    let socket = ws.open(url,{protocols:[session.burst_protocol],max_message_bytes:session.max_frame,timeout_ms:5000})^
    let result = client(socket,handle,credentials,expected,authorize,options,report) ^ { ^ }
    ws.close(socket) ^ { null }
    if (result is error) raise result
    return result
}
pub pn accept(listener, handle, credentials, authorize, options, report=null) any^ {
    let socket = ws.accept(listener,1000)^
    let result = if (socket.protocol == session.burst_protocol)
        server(socket,handle,credentials,authorize,options,report) ^ { ^ }
        else peer.serve_connection(socket,handle,credentials,authorize) ^ { ^ }
    if (socket.protocol != session.burst_protocol and not (result is error)) agent.materialize(handle)^
    ws.close(socket) ^ { null }
    if (result is error) raise result
    return result
}
