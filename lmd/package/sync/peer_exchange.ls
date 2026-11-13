// bounded authenticated checkpoint exchange; each imported frame commits before its ACK.
import local: .local
import protocol: .protocol
import workspace: .workspace
import auth: .auth
import codec: .codec
import v: .schema
import crypto: lambda.io.crypto
import document_state: .document_state
import maintenance: .maintenance_state
import document_exchange: .document_exchange
import owner: .task_owner

pub let page_size = 64
pub let fragment_size = 32768
pub fn token(current) any^ {
    let value = {clock:current.endpoint.replica.clock,epoch:current.endpoint.replica.epoch,
        documents:[for (record in document_state.all(current)) {id:record.id,clock:record.endpoint.replica.clock,
            epoch:record.endpoint.replica.epoch,origin:record.origin.id,
            versions:sort([for (saved in record.session.versions) saved.id])^}],
        retention:maintenance.get(current).manifest,cache:current.cache_root}
    return {*:value,digest:codec.digest(value)^}
}
pub pn current_token(handle) any^ {
    if (handle.keys.owner != null and handle.keys.token_cached == true and handle.keys.owner != self()) {
        return owner.current(handle.keys.owner)^
    }
    return token(local.status(handle)^.state)^
}
pub pn offer(handle, credentials, extended=false) any^ {
    if (handle.keys.owner != null and handle.keys.owner != self()) {
        return owner.cached(handle.keys.owner,pn (cache)=> {
            return offer({*:handle,keys:{*:handle.keys,cached_head:cache}},credentials,extended)^
        })^
    }
    let loaded = local.status(handle)^
    let stored = loaded.state
    let current = stored.endpoint
    let frames = protocol.frames(current.replica,workspace.model,65536)^
    let digest = frames[0].body.checkpoint
    let proof = {id:digest,kind:'checkpoint',actor:credentials.device.id,
        signature:crypto.sign(credentials.seed,auth.checkpoint_bytes(frames[0],digest)^)^}
    let ids = local.inventory(handle,false)^
    let documents = document_state.all(stored)
    return {frames:frames,proof:proof,ids:ids,documents:documents,token:token(stored)^,repository_cache:loaded.repository_cache,
        summary:{version:3,space:credentials.space,
        digest:digest,clock:current.replica.clock,objects:len(ids),frames:len(frames),
        documents:document_exchange.catalog(documents)^,retention:maintenance.get(stored).manifest,cache:stored.cache_root,
        *:if (extended) {epoch:current.replica.epoch} else ({})}}
}
pub fn summary(value, space) any^ {
    v.record(value,["version","space","digest","clock","objects","frames"],["documents","retention","cache","epoch"])^
    if (not contains([1,2,3],value.version)^ or value.space != space or not (value.digest is binary) or len(value.digest)^ != 32 or
        not v.natural(value.objects)^ or value.objects > 100000 or not v.natural(value.frames)^ or
        value.frames < 1 or value.frames > 8192) raise error("sync peer: invalid offer")
    v.clock(value.clock)^
    if (value.epoch != null and not v.natural(value.epoch)^) raise error("sync peer: invalid offer epoch")
    document_exchange.validate_catalog(if (value.documents == null) [] else value.documents)^
    return true
}
pub fn object_id(id) any^ {
    if (not (id is binary) or len(id) != 32) raise error("sync peer: invalid object ID")
    return true
}
pub fn offset(value) any^ {
    if (not v.natural(value)^ or value > 8388608) raise error("sync peer: invalid offset")
    return true
}
pub fn object_ids(ids) any^ {
    if (not (ids is array) or len(ids) > page_size) raise error("sync peer: inventory page limit")
    for (id in ids) { object_id(id)^ }
    if (len(unique(ids)) != len(ids)) raise error("sync peer: duplicate object ID")
    return true
}
pub pn object_text(handle, id) any^ {
    object_id(id)^
    let encoded = codec.encode(local.export_objects(handle,[id])^[0])^
    if (len(encoded) > 8388608) raise error("sync peer: encoded object limit")
    return encoded
}
pub fn fragment(id, encoded, start) any^ {
    offset(start)^
    if (start >= len(encoded)) raise error("sync peer: object offset out of bounds")
    return {id:id,offset:start,total:len(encoded),text:slice(encoded,start,start+fragment_size)^}
}
pub pn import_fragment(handle, pending, part) any^ {
    v.fields(part,["id","offset","total","text"])^
    object_id(part.id)^
    offset(part.offset)^
    if (not v.natural(part.total)^ or part.total < 1 or part.total > 8388608 or
        not (part.text is string) or len(part.text)^ < 1 or len(part.text)^ > fragment_size or
        part.offset+len(part.text)^ > part.total) raise error("sync peer: invalid object fragment")
    let base = if (part.offset == 0) {id:part.id,total:part.total,text:""} else pending
    if (base == null or base.id != part.id or base.total != part.total or len(base.text)^ != part.offset)
        raise error("sync peer: noncontiguous object fragment")
    let text = base.text ++ part.text
    if (len(text)^ < part.total) { return {id:part.id,total:part.total,text:text} }
    let record = codec.decode(text)^
    if (record.id != part.id) raise error("sync peer: object descriptor mismatch")
    local.import_objects(handle,[record])^
    return null
}
pub pn receive_frame(handle, credentials, peer, frame) any^ {
    v.fields(frame,["packet","proof"])^
    let packet = frame.packet
    if (packet.kind != 'checkpoint_frame' or frame.proof.actor != peer.id)
        raise error("sync peer: expected paired checkpoint authority")
    let grants = [{actor:peer.id,document:credentials.space,profile:'workspace',epoch:packet.epoch,
        schema:workspace.model,permissions:['checkpoint'],public_key:peer.public_key}]
    let admission = auth.admission(packet,[frame.proof],grants)^
    return local.receive(handle,packet,admission)^
}

// outgoing data is pinned per connection; requests cannot silently switch a checkpoint mid-transfer.
pub pn request(handle, credentials, peer, outgoing, pending, command) any^ {
    v.fields(command,["kind","body"])^
    let body = command.body
    if (command.kind == 'head') {
        v.fields(body,[])^
        return {body:outgoing.summary,pending:pending}
    }
    if (command.kind == 'frontier') {
        v.fields(body,[])^
        let token = current_token(handle)^
        return {body:{clock:token.clock,epoch:token.epoch},pending:pending}
    }
    if (command.kind == 'inventory') {
        v.fields(body,["offset"])^
        offset(body.offset)^
        if (body.offset > len(outgoing.ids)^) raise error("sync peer: inventory offset out of bounds")
        return {body:slice(outgoing.ids,body.offset,body.offset+page_size)^,pending:pending}
    }
    if (command.kind == 'object') {
        v.fields(body,["id","offset"])^
        if (not contains([*outgoing.ids,*[for (record in outgoing.documents) record.export_id]],body.id)^)
            raise error("sync peer: unadvertised object")
        return {body:fragment(body.id,object_text(handle,body.id)^,body.offset)^,pending:pending}
    }
    if (command.kind == 'frame') {
        v.fields(body,["index"])^
        if (not v.natural(body.index)^ or body.index >= len(outgoing.frames)^) raise error("sync peer: invalid frame index")
        return {body:{packet:outgoing.frames[body.index],proof:outgoing.proof},pending:pending}
    }
    if (command.kind == 'missing') {
        object_ids(body)^
        let have = local.inventory(handle)^
        return {body:[for (id in body where not contains(have,id)^) id],pending:pending}
    }
    if (command.kind == 'put_object') {
        return {body:true,pending:import_fragment(handle,pending,body)^}
    }
    if (command.kind == 'cache_root') {
        local.register_cache(handle,body)^
        return {body:true,pending:pending}
    }
    if (command.kind == 'retention_root') {
        local.register_retention(handle,body)^
        return {body:true,pending:pending}
    }
    if (command.kind == 'put_frame') {
        let applied = receive_frame(handle,credentials,peer,body)^
        return {body:applied.state.endpoint.replica.clock,pending:pending}
    }
    if (command.kind == 'ack') {
        local.acknowledge(handle,peer.id,body)^
        return {body:local.status(handle)^.state.endpoint.replica.clock,pending:pending}
    }
    if (command.kind == 'document_need') {
        return {body:document_exchange.need(handle,body)^,pending:pending}
    }
    if (command.kind == 'document_pull') {
        v.fields(body,["id","have","index"])^
        let record = v.lookup(outgoing.documents,body.id)^
        if (record == null) raise error("sync document: unadvertised document")
        return {body:document_exchange.reply(record,credentials,body.have,body.index)^,pending:pending}
    }
    if (command.kind == 'document_packet') {
        return {body:document_exchange.receive_packet(handle,credentials,peer,body)^,pending:pending}
    }
    if (command.kind == 'document_root') {
        local.register_documents(handle,[body])^
        let record = document_state.get(local.status(handle)^.state,body.id)^
        return {body:protocol.have(record.endpoint.replica,record.endpoint.schema)^,pending:pending}
    }
    if (command.kind == 'document_ack') {
        v.fields(body,["id","clock","object"])^
        document_exchange.acknowledge(handle,body.id,peer.id,body.clock,body.object)^
        return {body:true,pending:pending}
    }
    raise error("sync peer: unsupported request")
}
