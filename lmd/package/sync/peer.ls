// native P2P adapter: transport waits are procedural; merge and admission reuse the protocol.
import ws: lambda.io.websocket
import wire: .peer_transport
import session: .peer_session
import exchange: .peer_exchange
import identity: .peer_identity
import local: .local
import core: .core
import codec: .codec
import v: .schema
import documents: .document_exchange
import document_state: .document_state
import protocol: .protocol

pn plain_receive(socket) any^ {
    let frame = wire.receive(socket,15000)^
    if (frame.type != "binary") raise error("sync peer: expected binary handshake")
    return codec.decode(frame.data,16384)^
}
pn plain_send(socket, value) any^ { wire.send(socket,binary(codec.encode(value)^)^)^ }
pub pn handshake(socket, credentials, role, expected=null, authorize=null, profile=session.protocol) any^ {
    let own = session.start(credentials,role,profile)^
    var remote = null
    if (role == 'client') {
        plain_send(socket,own.hello)^
        remote = plain_receive(socket)^
    } else {
        remote = plain_receive(socket)^
        session.transcript(credentials,own,remote)^
        plain_send(socket,own.hello)^
    }
    if (expected != null and remote.device != expected) raise error("sync peer: endpoint identity pin mismatch")
    let proof = session.proof(credentials,own,remote)^
    var signature = null
    if (role == 'client') { plain_send(socket,proof)^; signature = plain_receive(socket)^ }
    else { signature = plain_receive(socket)^ }
    let established = session.finish(credentials,own,remote,signature)^
    if (role == 'server') { plain_send(socket,proof)^ }
    return {*:established,authorize:authorize}
}
pn authorized(channel) any^ {
    if (channel.authorize != null and channel.authorize(channel.peer.id)^ != channel.peer)
        raise error("sync peer: current device grant changed")
    return true
}
pub pn send_record(socket, channel, payload) any^ {
    authorized(channel)^
    let sealed = session.seal(channel,payload)^
    wire.send(socket,sealed.bytes)^
    return sealed.session
}
pub pn read_record(socket, channel, timeout_ms=30000) any^ {
    let frame = wire.receive(socket,timeout_ms)^
    if (frame.type != "binary") raise error("sync peer: connection ended before acknowledgment")
    let decoded = session.unseal(channel,frame.data)^
    authorized(channel)^
    return decoded
}
pub pn request(socket, channel, kind, body) any^ {

    let sent = send_record(socket,channel,{kind:kind,body:body})^
    var response = read_record(socket,sent)^
    // A peer may announce a new batch just as this side starts its next round.
    var notices = 0
    while (session.is_burst(sent.profile)^ and response.value.kind == 'changed') {
        if (notices >= 64) raise error("sync peer: excessive change notices")
        response = read_record(socket,response.session)^
        notices = notices+1
    }
    v.fields(response.value,["ok","body"])^
    if (response.value.ok != true) raise error("sync peer: remote request rejected")

    return {session:response.session,value:response.value.body}
}
pub pn serve_connection(socket, handle, credentials, authorize=null, observer=null) any^ {
    let channel = handshake(socket,credentials,'server',null,authorize)^
    local.scan(handle)^
    return serve_round(socket,channel,handle,credentials,observer)^.result
}
pub pn serve_round(socket, initial_channel, handle, credentials, observer=null, first=null) any^ {
    var channel = initial_channel
    let offered_have = if (channel.profile == session.batch_protocol and first != null) first.value.body else null
    let outgoing = exchange.offer(handle,credentials,session.is_burst(channel.profile)^,
        channel.profile == session.batch_protocol,offered_have)^
    var pending = null
    var requests = 0
    while (requests < 100000) {
        let incoming = if (requests == 0 and first != null) first else read_record(socket,channel)^
        channel = incoming.session
        if (incoming.value == {kind:'done',body:null}) {
            channel = send_record(socket,channel,{ok:true,body:true})^
            local.release_staging(handle,outgoing.ids)^
            return {session:channel,token:outgoing.token,
                result:{peer:channel.peer.id,received:local.status(handle)^.state.endpoint.replica.clock}}
        }

        let result = exchange.request(handle,credentials,channel.peer,outgoing,pending,incoming.value) ^ { ^ }

        if (result is error) {
            send_record(socket,channel,{ok:false,body:null})^
            raise result
        }
        pending = result.pending
        channel = send_record(socket,channel,{ok:true,body:result.body})^
        requests = requests+1
        if (observer != null) observer({direction:'serve',requests:requests,kind:incoming.value.kind})^
    }
    raise error("sync peer: connection request budget exhausted")
}

pub pn synchronize(handle, credentials, url, expected, authorize=null) any^ {
    identity.member(credentials,expected)^
    local.scan(handle)^
    let socket = ws.open(url,{protocols:[session.protocol],max_message_bytes:session.max_frame,timeout_ms:15000})^
    let result = exchange_connection(socket,handle,credentials,expected,authorize) ^ { ^ }
    wire.close(socket) ^ { null }
    if (result is error) raise result
    return result
}
pub pn exchange_connection(socket, handle, credentials, expected, authorize=null, observer=null) any^ {
    let channel = handshake(socket,credentials,'client',expected,authorize)^
    return exchange_round(socket,channel,handle,credentials,observer)^.result
}
pub pn exchange_round(socket, initial_channel, handle, credentials, observer=null) any^ {
    var channel = initial_channel
    let own = exchange.current_token(handle)^
    let head = request(socket,channel,'head',if (channel.profile == session.batch_protocol)
        exchange.have_frontier(credentials.space,own)^ else ({}))^
    channel = head.session
    exchange.summary(head.value,credentials.space)^
    let remote = head.value
    let covered = remote.epoch == own.epoch and core.dominates(own.clock,remote.clock)^
    let available = local.inventory(handle)^
    var index = 0
    var downloaded = 0
    var uploaded = 0
    var missing_pull = []
    while (index < remote.objects) {
        let page = request(socket,channel,'inventory',{offset:index})^
        channel = page.session
        exchange.object_ids(page.value)^
        if (len(page.value)^ == 0 or index+len(page.value)^ > remote.objects) raise error("sync peer: truncated inventory")
        missing_pull = [*missing_pull,*[for (id in page.value where not contains(available,id)^) id]]
        index = index+len(page.value)^
    }
    var pulled_bundle = false
    if (channel.profile == session.batch_protocol and remote.batch_frames == true and not covered and
        remote.retention == own.retention and remote.cache == own.cache and len(missing_pull)^ <= exchange.page_size) {
        let response = request(socket,channel,'bundle',missing_pull)^
        channel = response.session
        if (response.value != null) {
            exchange.bundle_objects(response.value,missing_pull)^
            exchange.frames_match(response.value.frames,remote)^
            exchange.receive_bundle(handle,credentials,channel.peer,response.value)^
            downloaded = len(missing_pull)^; pulled_bundle = true
        }
    }
    if (not pulled_bundle) {
        for (id in missing_pull) {
            channel = download_object(socket,channel,handle,id)^
            downloaded = downloaded+1
            if (observer != null) observer({direction:'pull',downloaded:downloaded,uploaded:uploaded})^
        }
    }
    local.register_retention(handle,remote.retention)^
    local.register_cache(handle,remote.cache)^
    index = if (pulled_bundle) remote.frames else 0
    if (not covered and not pulled_bundle and remote.batch_frames == true and channel.profile == session.batch_protocol) {
        let response = request(socket,channel,'frames',{})^
        channel = response.session
        exchange.frames_match(response.value,remote)^
        exchange.receive_frames(handle,credentials,channel.peer,response.value)^
        index = remote.frames
    }
    while (not covered and index < remote.frames) {
        let response = request(socket,channel,'frame',{index:index})^
        channel = response.session
        exchange.frame_matches(response.value,remote,index)^
        exchange.receive_frame(handle,credentials,channel.peer,response.value)^
        index = index+1
    }
    let pulled_documents = pull_documents(socket,channel,handle,credentials,remote.documents)^
    channel = pulled_documents.session
    // the server's ACK records the exact pinned channel whose dependencies have committed here.
    let ack = request(socket,channel,'ack',remote.clock)^
    channel = ack.session
    let outgoing = exchange.offer(handle,credentials,session.is_burst(channel.profile)^,
        channel.profile == session.batch_protocol,if (channel.profile == session.batch_protocol)
            exchange.have_frontier(credentials.space,remote)^ else null)^
    index = 0
    var missing_push = []
    while (index < len(outgoing.ids)^) {
        let page = slice(outgoing.ids,index,index+exchange.page_size)^
        let missing = request(socket,channel,'missing',page)^
        channel = missing.session
        exchange.object_ids(missing.value)^
        for (id in missing.value) {
            if (not contains(page,id)^) raise error("sync peer: requested unadvertised object")
            missing_push = [*missing_push,id]
        }
        index = index+len(page)
    }
    let controls_match = outgoing.summary.retention == remote.retention and outgoing.summary.cache == remote.cache
    var accepted = []
    var covered_outgoing = false
    if (channel.profile == session.batch_protocol) {
        covered_outgoing = remote.epoch == outgoing.summary.epoch and core.dominates(remote.clock,outgoing.summary.clock)^
        if (covered_outgoing) { accepted = remote.clock }
    } else if (session.is_burst(channel.profile)^) {
        let known = request(socket,channel,'frontier',{})^
        channel = known.session
        v.fields(known.value,["clock","epoch"])^
        v.clock(known.value.clock)^
        covered_outgoing = known.value.epoch == outgoing.summary.epoch and core.dominates(known.value.clock,outgoing.summary.clock)^
        if (covered_outgoing) { accepted = known.value.clock }
    }
    let combined = if (not covered_outgoing and controls_match and channel.profile == session.batch_protocol)
        exchange.bundle(handle,outgoing.batch,missing_push)^ else null
    if (combined != null) {
        let response = request(socket,channel,'put_bundle',combined)^
        channel = response.session
        accepted = response.value; uploaded = len(missing_push)^; covered_outgoing = true
    }
    if (combined == null) {
        for (id in missing_push) {
            channel = upload_object(socket,channel,handle,id)^
            uploaded = uploaded+1
            if (observer != null) observer({direction:'push',downloaded:downloaded,uploaded:uploaded})^
        }
    }
    if (channel.profile != session.batch_protocol or not controls_match) {
        let retirement = request(socket,channel,'retention_root',outgoing.summary.retention)^
        channel = retirement.session
        let availability = request(socket,channel,'cache_root',outgoing.summary.cache)^
        channel = availability.session
    }
    if (not covered_outgoing and outgoing.batch != null and channel.profile == session.batch_protocol) {
        let response = request(socket,channel,'put_frames',outgoing.batch)^
        channel = response.session
        accepted = response.value
        covered_outgoing = true
    }
    for (packet in (if (covered_outgoing) [] else outgoing.frames)) {
        let response = request(socket,channel,'put_frame',{packet:packet,proof:outgoing.proof})^
        channel = response.session
        accepted = response.value
    }
    if (not core.dominates(accepted,outgoing.summary.clock)^) raise error("sync peer: incomplete peer acknowledgment")
    local.acknowledge(handle,channel.peer.id,outgoing.summary.clock)^
    let pushed_documents = push_documents(socket,channel,handle,credentials,outgoing.documents)^
    channel = pushed_documents.session
    let ended = request(socket,channel,'done',null)^
    local.release_staging(handle,available)^
    return {session:ended.session,token:outgoing.token,result:{peer:channel.peer.id,downloaded:downloaded,uploaded:uploaded,
        frontier:outgoing.summary.clock,replicated:true,cloud_saved:false,
        document_packets:pulled_documents.packets+pushed_documents.packets,
        document_roots:pulled_documents.roots+pushed_documents.roots}}
}

pn download_object(socket, channel, handle, id) any^ {
    var active = channel
    var pending = null
    var position = 0
    var complete = false
    while (not complete) {
        let received = request(socket,active,'object',{id:id,offset:position})^
        active = received.session
        if (received.value.id != id or received.value.offset != position)
            raise error("sync peer: object response mismatch")
        pending = exchange.import_fragment(handle,pending,received.value)^
        complete = pending == null
        position = position+len(received.value.text)^
    }
    return active
}
pn upload_object(socket, channel, handle, id) any^ {
    var active = channel
    let text = exchange.object_text(handle,id)^
    var position = 0
    while (position < len(text)) {
        let fragment = exchange.fragment(id,text,position)^
        let sent = request(socket,active,'put_object',fragment)^
        active = sent.session
        position = position+len(fragment.text)^
    }
    return active
}
pn pull_documents(socket, channel, handle, credentials, catalog) any^ {
    var active = channel
    var packets = 0
    var roots = 0
    let selected = if (catalog == null) [] else catalog
    for (remote in selected) {
        let needed = documents.need(handle,remote)^
        if (needed.root) {
            active = download_object(socket,active,handle,remote.object)^
            local.register_documents(handle,[{id:remote.id,object:remote.object}])^
            roots = roots+1
        } else {
            var have = needed.have
            var index = 0
            var rounds = 0
            while (not core.dominates(have.body.clock,remote.have.body.clock)^) {
                if (rounds >= 8192) raise error("sync document: pull round budget")
                let response = request(socket,active,'document_pull',{id:remote.id,have:have,index:index})^
                active = response.session
                have = documents.receive_packet(handle,credentials,active.peer,response.value)^
                let packet = response.value.body.packet
                index = if (packet.kind == 'checkpoint_frame') packet.body.index+1 else 0
                packets = packets+1
                rounds = rounds+1
            }
        }
        let ack = request(socket,active,'document_ack',{id:remote.id,clock:remote.have.body.clock,object:remote.object})^
        active = ack.session
    }
    return {session:active,packets:packets,roots:roots}
}
pn push_documents(socket, channel, handle, credentials, records) any^ {
    var active = channel
    var packets = 0
    var roots = 0
    for (record in records) {
        let remote = request(socket,active,'document_need',documents.descriptor(record)^)^
        active = remote.session
        var have = remote.value.have
        if (remote.value.root) {
            active = upload_object(socket,active,handle,record.export_id)^
            let registered = request(socket,active,'document_root',{id:record.id,object:record.export_id})^
            active = registered.session
            have = registered.value
            roots = roots+1
        } else {
            var index = 0
            var rounds = 0
            while (not core.dominates(have.body.clock,record.endpoint.replica.clock)^) {
                if (rounds >= 8192) raise error("sync document: push round budget")
                let envelope = documents.reply(record,credentials,have,index)^
                let sent = request(socket,active,'document_packet',envelope)^
                active = sent.session
                have = sent.value
                let packet = envelope.body.packet
                index = if (packet.kind == 'checkpoint_frame') packet.body.index+1 else 0
                packets = packets+1
                rounds = rounds+1
            }
        }
        if (not core.dominates(have.body.clock,record.endpoint.replica.clock)^)
            raise error("sync document: incomplete document acknowledgment")
        documents.acknowledge(handle,record.id,active.peer.id,record.endpoint.replica.clock,record.export_id)^
    }
    return {session:active,packets:packets,roots:roots}
}

pub pn listener(port, bind="127.0.0.1", burst=false) any^ {
    return ws.listen(port,{bind:bind,protocols:if (burst) [session.protocol,session.batch_protocol,session.burst_protocol] else [session.protocol],
        max_message_bytes:session.max_frame})^
}
pub pn accept(listener, handle, credentials, timeout_ms=30000, authorize=null) any^ {
    let socket = ws.accept(listener,timeout_ms)^
    let result = serve_connection(socket,handle,credentials,authorize) ^ { ^ }
    wire.close(socket) ^ { null }
    if (result is error) raise result
    return result
}
