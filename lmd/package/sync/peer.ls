// native P2P adapter: transport waits are procedural; merge and admission reuse the protocol.
import ws: lambda.io.websocket
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
    let frame = ws.receive(socket,15000)^
    if (frame.type != "binary") raise error("sync peer: expected binary handshake")
    return codec.decode(frame.data,16384)^
}
pn plain_send(socket, value) any^ { ws.send(socket,binary(codec.encode(value)^)^)^ }
pub pn handshake(socket, credentials, role, expected=null, authorize=null) any^ {
    let own = session.start(credentials,role)^
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
pn send_record(socket, channel, payload) any^ {
    authorized(channel)^
    let sealed = session.seal(channel,payload)^
    ws.send(socket,sealed.bytes)^
    return sealed.session
}
pn read_record(socket, channel) any^ {
    let frame = ws.receive(socket,30000)^
    if (frame.type != "binary") raise error("sync peer: connection ended before acknowledgment")
    let decoded = session.unseal(channel,frame.data)^
    authorized(channel)^
    return decoded
}
pub pn request(socket, channel, kind, body) any^ {
    let sent = send_record(socket,channel,{kind:kind,body:body})^
    let response = read_record(socket,sent)^
    v.fields(response.value,["ok","body"])^
    if (response.value.ok != true) raise error("sync peer: remote request rejected")
    return {session:response.session,value:response.value.body}
}
pub pn serve_connection(socket, handle, credentials, authorize=null) any^ {
    var channel = handshake(socket,credentials,'server',null,authorize)^
    local.scan(handle)^
    let outgoing = exchange.offer(handle,credentials)^
    var pending = null
    var requests = 0
    while (requests < 100000) {
        let incoming = read_record(socket,channel)^
        channel = incoming.session
        if (incoming.value == {kind:'done',body:null}) {
            channel = send_record(socket,channel,{ok:true,body:true})^
            return {peer:channel.peer.id,received:local.status(handle)^.state.endpoint.replica.clock}
        }
        let result = exchange.request(handle,credentials,channel.peer,outgoing,pending,incoming.value) ^ { ^ }
        if (result is error) {
            send_record(socket,channel,{ok:false,body:null})^
            raise result
        }
        pending = result.pending
        channel = send_record(socket,channel,{ok:true,body:result.body})^
        requests = requests+1
    }
    raise error("sync peer: connection request budget exhausted")
}

pub pn synchronize(handle, credentials, url, expected, authorize=null) any^ {
    identity.member(credentials,expected)^
    local.scan(handle)^
    let socket = ws.open(url,{protocols:[session.protocol],max_message_bytes:session.max_frame,timeout_ms:15000})^
    let result = exchange_connection(socket,handle,credentials,expected,authorize) ^ { ^ }
    ws.close(socket) ^ { null }
    if (result is error) raise result
    return result
}
pn exchange_connection(socket, handle, credentials, expected, authorize) any^ {
    var channel = handshake(socket,credentials,'client',expected,authorize)^
    let head = request(socket,channel,'head',{})^
    channel = head.session
    exchange.summary(head.value,credentials.space)^
    let remote = head.value
    let available = local.inventory(handle)^
    var index = 0
    var downloaded = 0
    var uploaded = 0
    while (index < remote.objects) {
        let page = request(socket,channel,'inventory',{offset:index})^
        channel = page.session
        exchange.object_ids(page.value)^
        if (len(page.value)^ == 0 or index+len(page.value)^ > remote.objects) raise error("sync peer: truncated inventory")
        for (id in page.value where not contains(available,id)^) {
            channel = download_object(socket,channel,handle,id)^
            downloaded = downloaded+1
        }
        index = index+len(page.value)^
    }
    index = 0
    while (index < remote.frames) {
        let response = request(socket,channel,'frame',{index:index})^
        channel = response.session
        if (response.value.packet.body.checkpoint != remote.digest or response.value.packet.body.index != index or
            response.value.packet.body.total != remote.frames) raise error("sync peer: checkpoint response mismatch")
        exchange.receive_frame(handle,credentials,channel.peer,response.value)^
        index = index+1
    }
    let pulled_documents = pull_documents(socket,channel,handle,credentials,remote.documents)^
    channel = pulled_documents.session
    // the server's ACK records the exact pinned channel whose dependencies have committed here.
    let ack = request(socket,channel,'ack',remote.clock)^
    channel = ack.session
    let outgoing = exchange.offer(handle,credentials)^
    index = 0
    while (index < len(outgoing.ids)^) {
        let page = slice(outgoing.ids,index,index+exchange.page_size)^
        let missing = request(socket,channel,'missing',page)^
        channel = missing.session
        exchange.object_ids(missing.value)^
        for (id in missing.value) {
            if (not contains(page,id)^) raise error("sync peer: requested unadvertised object")
            channel = upload_object(socket,channel,handle,id)^
            uploaded = uploaded+1
        }
        index = index+len(page)
    }
    var accepted = []
    for (packet in outgoing.frames) {
        let response = request(socket,channel,'put_frame',{packet:packet,proof:outgoing.proof})^
        channel = response.session
        accepted = response.value
    }
    if (not core.dominates(accepted,outgoing.summary.clock)^) raise error("sync peer: incomplete peer acknowledgment")
    local.acknowledge(handle,channel.peer.id,outgoing.summary.clock)^
    let pushed_documents = push_documents(socket,channel,handle,credentials,outgoing.documents)^
    channel = pushed_documents.session
    let ended = request(socket,channel,'done',null)^
    return {peer:channel.peer.id,downloaded:downloaded,uploaded:uploaded,
        frontier:outgoing.summary.clock,replicated:true,cloud_saved:false,
        document_packets:pulled_documents.packets+pushed_documents.packets,
        document_roots:pulled_documents.roots+pushed_documents.roots}
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

pub pn listener(port, bind="127.0.0.1") any^ {
    return ws.listen(port,{bind:bind,protocols:[session.protocol],max_message_bytes:session.max_frame})^
}
pub pn accept(listener, handle, credentials, timeout_ms=30000, authorize=null) any^ {
    let socket = ws.accept(listener,timeout_ms)^
    let result = serve_connection(socket,handle,credentials,authorize) ^ { ^ }
    ws.close(socket) ^ { null }
    if (result is error) raise result
    return result
}
