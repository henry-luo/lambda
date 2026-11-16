// A short-lived authenticated channel carries several pinned exchange rounds.
import peer: .peer
import session: .peer_session
import exchange: .peer_exchange
import agent: .peer_agent
import ws: lambda.io.websocket
import timing: .timing
import schema: .schema
import wire: .peer_transport
import owner: .task_owner
import fast: .peer_fast
import core: .core
import local: .local

pn changed_round(socket, channel, handle, credentials) any^ {
    let outgoing = fast.prepare(handle,credentials,channel.remote_have)^
    if (outgoing == null) { return null }
    let response = peer.request(socket,channel,'change',outgoing.data)^
    let body = response.value
    schema.fields(body,["accepted","exchange","clock"])^
    if (not (body.accepted is bool)) raise error("sync fast: invalid response")
    schema.clock(body.clock)^
    if (body.exchange == null) { return {session:response.session,retry:true} }
    if (body.accepted != true or body.clock != body.exchange.summary.clock or
        not core.dominates(body.clock,outgoing.data.summary.clock)^)
        raise error("sync fast: incomplete acknowledgment")
    let received = fast.accept(handle,credentials,channel.peer,body.exchange)^
    let completed = peer.request(socket,response.session,'change_ack',
        {clock:if (received == null) null else body.exchange.summary.clock},
        if (received == null) null else pn ()=> { return agent.materialize(handle)^ })^
    if (received == null) { return {session:completed.session,retry:true} }
    return {session:{*:completed.session,remote_have:body.exchange.have},token:fast.token(body.exchange)^,
        result:{peer:channel.peer.id,uploaded:len(outgoing.data.bundle.objects)^,
            downloaded:len(body.exchange.bundle.objects)^,replicated:true,cloud_saved:false}}
}

pn ready(socket, handle, token, timeout) bool^ {
    if (socket.transport != 'buffered' or handle.keys.owner == null) { return true }
    let network = start(wire.ready,[socket])
    let changed = start(owner.changed,[handle.keys.owner,token])
    let selected = select(network,changed,timeout:int(ceil(timeout))^) ^ { ^ }
    cancel(network); cancel(changed)
    wait(network) ^ { null }; wait(changed) ^ { null }
    if (selected is error) {
        if (selected.code == 310) { return false }
        raise selected
    }
    return selected == network
}

pn notify(report, event) any^ { if (report != null) report(event)^; return true }
pn finished(handle, report, round, count) any^ {

    agent.materialize(handle)^

    let installed = agent.resume_installation(handle)^.state

    notify(report,{type:'round',peer:round.result.peer,token:round.token,progress:round.result,rounds:count,
        installed_frontier:installed.base.checkpoint.frontier,
        installation:installed.installation != null,review:installed.review != null,
        warning:if (installed.installation == null) null else installed.installation.blocked.reason})^
    return true
}
pub pn client(socket, handle, credentials, expected, authorize, options, report=null) any^ {
    var channel = peer.handshake(socket,credentials,'client',expected,authorize,socket.protocol)^
    notify(report,{type:'connected',peer:channel.peer.id,role:channel.role,profile:channel.profile})^
    let started = timing.milliseconds()
    var last_activity = started
    var token = null
    var needed = true
    var rounds = 0
    var inventory_at = started
    let verify_ms = if (options.verify_ms == null) 60000 else options.verify_ms
    while (timing.milliseconds()-started < options.burst_ms and rounds < 1024) {
        if (needed or token != exchange.current_token(handle)^) {
            var round = if (channel.profile == session.fast_protocol and channel.remote_have != null and
                timing.milliseconds()-inventory_at < verify_ms) changed_round(socket,channel,handle,credentials)^ else null
            if (round != null) { channel = round.session }
            if (round == null or round.retry == true) {
                round = peer.exchange_round(socket,channel,handle,credentials,null,
                    pn ()=> { return agent.materialize(handle)^ })^
                inventory_at = timing.milliseconds()
            }
            channel = round.session; token = round.token; rounds = rounds+1
            finished(handle,report,round,rounds)^
            last_activity = timing.milliseconds(); needed = false
        }
        if (timing.milliseconds()-last_activity >= options.idle_ms) { break }
        if (not ready(socket,handle,token,max(1,options.idle_ms-(timing.milliseconds()-last_activity)))^) {
            continue
        }
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
    var channel = peer.handshake(socket,credentials,'server',null,authorize,socket.protocol)^
    notify(report,{type:'connected',peer:channel.peer.id,role:channel.role,profile:channel.profile})^
    let started = timing.milliseconds()
    var last_activity = started
    var announced = exchange.current_token(handle)^
    var delivered = null
    var rounds = 0
    var pending_fast = null
    while (timing.milliseconds()-started < options.burst_ms+1000 and rounds < 1024) {
        let current = exchange.current_token(handle)^
        if (current != announced) {
            channel = peer.send_record(socket,channel,{kind:'changed',body:current.digest})^
            announced = current
        }
        if (timing.milliseconds()-last_activity >= options.idle_ms+1000) { break }
        if (not ready(socket,handle,announced,max(1,options.idle_ms+1000-(timing.milliseconds()-last_activity)))^) {
            continue
        }
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
        schema.fields((incoming^).value,["kind","body"])^
        if (channel.profile == session.fast_protocol and (incoming^).value.kind == 'change') {
            if (pending_fast != null) raise error("sync fast: outstanding exchange")
            let body = (incoming^).value.body
            let accepted = fast.accept(handle,credentials,channel.peer,body)^
            let outgoing = if (accepted == null) null else fast.prepare(handle,credentials,body.have)^
            let current = exchange.current_token(handle)^
            channel = peer.send_record(socket,channel,{ok:true,body:{accepted:accepted != null,
                exchange:if (outgoing == null) null else outgoing.data,
                clock:if (outgoing == null) current.clock else outgoing.data.summary.clock}})^
            announced = if (outgoing == null) current else outgoing.token
            if (outgoing != null) { pending_fast = outgoing }
            // Metadata is durable before the reply; guarded writes can now overlap client bookkeeping.
            if (accepted != null) { agent.materialize(handle)^ }
            last_activity = timing.milliseconds()
            continue
        }
        if (channel.profile == session.fast_protocol and (incoming^).value.kind == 'change_ack') {
            if (pending_fast == null) raise error("sync fast: no pending acknowledgment")
            let body = (incoming^).value.body
            schema.fields(body,["clock"])^
            if (body.clock != null) {
                schema.clock(body.clock)^
                if (body.clock != pending_fast.data.summary.clock) raise error("sync fast: changed acknowledgment")
                local.acknowledge(handle,channel.peer.id,body.clock)^
            }
            channel = peer.send_record(socket,channel,{ok:true,body:true})^
            if (body.clock != null) {
                delivered = pending_fast.token; rounds = rounds+1
                finished(handle,report,{token:delivered,result:{peer:channel.peer.id,replicated:true}},rounds)^
            }
            pending_fast = null; last_activity = timing.milliseconds()
            continue
        }
        if ((incoming^).value.kind != 'head' or
            (not session.is_batch(channel.profile)^ and (incoming^).value.body != {}))
            raise error("sync burst: expected a new round")
        let round = peer.serve_round(socket,channel,handle,credentials,null,incoming^,
            pn ()=> { return agent.materialize(handle)^ })^
        channel = round.session; delivered = round.token; announced = round.token; rounds = rounds+1
        finished(handle,report,round,rounds)^
        last_activity = timing.milliseconds()
    }
    return {peer:channel.peer.id,token:delivered,rounds:rounds}
}
pub pn connect(url, handle, credentials, expected, authorize, options, report=null) any^ {
    let socket = ws.open(url,{protocols:[session.fast_protocol,session.batch_protocol,session.burst_protocol],max_message_bytes:session.max_frame,timeout_ms:5000})^
    let reader = start(wire.reader,[socket])
    let buffered = {transport:'buffered',handle:socket,reader:reader,protocol:socket.protocol}
    let result = client(buffered,handle,credentials,expected,authorize,options,report) ^ { ^ }
    // Cancelling a pending read can abort the socket; flush the final ACK/close first.
    ws.close(socket) ^ { null }
    wire.finish(buffered)^; wait(reader) ^ { null }
    if (result is error) raise result
    return result
}
pub pn accept(listener, handle, credentials, authorize, options, report=null) any^ {
    let socket = ws.accept(listener,1000)^
    let reader = start(wire.reader,[socket])
    let buffered = {transport:'buffered',handle:socket,reader:reader,protocol:socket.protocol}
    let result = if (session.is_burst(socket.protocol)^)
        server(buffered,handle,credentials,authorize,options,report) ^ { ^ }
        else peer.serve_connection(buffered,handle,credentials,authorize) ^ { ^ }
    if (not session.is_burst(socket.protocol)^ and not (result is error)) agent.materialize(handle)^
    // Preserve the final response before reader cancellation tears down a parked read.
    ws.close(socket) ^ { null }
    wire.finish(buffered)^; wait(reader) ^ { null }
    if (result is error) raise result
    return result
}
