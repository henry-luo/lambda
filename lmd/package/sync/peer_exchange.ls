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
import session: .peer_session
import core: .core

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
pub fn have_frontier(space, frontier) any^ => protocol.have_frontier(space,'workspace',frontier.epoch,
    workspace.model,workspace.empty(space).nodes[0],frontier.clock)^
pub pn offer(handle, credentials, extended=false, batched=false, peer_have=null) any^ {
    if (handle.keys.owner != null and handle.keys.owner != self()) {
        return owner.cached(handle.keys.owner,pn (cache)=> {
            return offer({*:handle,keys:{*:handle.keys,cached_head:cache}},credentials,extended,batched,peer_have)^
        })^
    }
    let loaded = local.status(handle)^
    let stored = loaded.state
    let current = stored.endpoint
    let selected = if (batched and peer_have != null) delta_frames(current,credentials,peer_have)^ else null
    let delta = selected != null
    var frames = []
    var proof = null
    var batch = selected
    if (delta) {
        frames = [for (frame in selected) frame.packet]
        proof = selected[0].proof
    }
    if (not delta) {
        frames = protocol.frames(current.replica,workspace.model,65536)^
        let digest = frames[0].body.checkpoint
        proof = {id:digest,kind:'checkpoint',actor:credentials.device.id,
            signature:crypto.sign(credentials.seed,auth.checkpoint_bytes(frames[0],digest)^)^}
        let candidates = if (batched) [for (packet in frames) {packet:packet,proof:proof}] else null
        batch = if (batched and batch_fits(candidates)^) candidates else null
    }
    let digest = if (delta) codec.digest(frames)^ else proof.id
    let ids = local.inventory(handle,false)^
    let documents = document_state.all(stored)
    return {frames:frames,proof:proof,batch:batch,peer_have:peer_have,ids:ids,documents:documents,
        token:token(stored)^,repository_cache:loaded.repository_cache,
        summary:{version:3,space:credentials.space,digest:digest,clock:current.replica.clock,
        objects:len(ids),frames:len(frames),documents:document_exchange.catalog(documents)^,
        retention:maintenance.get(stored).manifest,cache:stored.cache_root,
        *:if (extended) {epoch:current.replica.epoch} else ({}),
        *:if (batched) {batch_frames:batch != null,delta:delta} else ({})}}
}
pub pn attest_delta(credentials, packet) any^ {
    let proof = {id:codec.digest(packet)^,kind:'checkpoint_delta',actor:credentials.device.id,
        signature:crypto.sign(credentials.seed,auth.checkpoint_delta_bytes(packet)^)^}
    return {packet:packet,proof:proof}
}
pn delta_frames(endpoint, credentials, peer_have) any^ {
    var have = peer_have
    var frames = []
    var rounds = 0
    while (rounds < 8192) {
        let packet = protocol.next(endpoint,have,128,8388608)^
        if (not contains(['transaction','updates'],packet.kind)^) { return null }
        frames = [*frames,attest_delta(credentials,packet)^]
        if (not batch_fits(frames)^) { return null }
        let operations = packet.body.operations
        let added = [for (actor in unique([for (op in operations) op.id[0]])^)
            {actor:actor,counter:max([for (op in operations where op.id[0] == actor) op.id[1]])}]
        let clock = core.clock_join(have.body.clock,added)^
        if (core.dominates(clock,endpoint.replica.clock)^) { return frames }
        if (clock == have.body.clock) { return null }
        have = {*:have,body:{clock:clock,floor:have.body.floor}}
        rounds = rounds+1
    }
    return null
}
fn batch_fits(frames) any^ => max(
    len(binary(codec.encode({kind:'put_frames',body:frames})^))^,
    len(binary(codec.encode({ok:true,body:frames})^))^) <= session.max_frame-32
pub fn summary(value, space) any^ {
    v.record(value,["version","space","digest","clock","objects","frames"],["documents","retention","cache","epoch","batch_frames","delta"])^
    if (not contains([1,2,3],value.version)^ or value.space != space or not (value.digest is binary) or len(value.digest)^ != 32 or
        not v.natural(value.objects)^ or value.objects > 100000 or not v.natural(value.frames)^ or
        value.frames < 1 or value.frames > 8192) raise error("sync peer: invalid offer")
    v.clock(value.clock)^
    if (value.epoch != null and not v.natural(value.epoch)^) raise error("sync peer: invalid offer epoch")
    if (value.batch_frames != null and not (value.batch_frames is bool)) raise error("sync peer: invalid batch flag")
    if (value.delta != null and not (value.delta is bool)) raise error("sync peer: invalid delta flag")
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
fn frame_admission(credentials, peer, frame) any^ {
    v.fields(frame,["packet","proof"])^
    let packet = frame.packet
    if (frame.proof.actor != peer.id)
        raise error("sync peer: expected paired checkpoint authority")
    let grants = [{actor:peer.id,document:credentials.space,profile:'workspace',epoch:packet.epoch,
        schema:workspace.model,permissions:['checkpoint'],public_key:peer.public_key}]
    return if (packet.kind == 'checkpoint_frame') auth.admission(packet,[frame.proof],grants)^
        else auth.checkpoint_delta_admission(packet,frame.proof,grants)^
}
pub pn receive_frame(handle, credentials, peer, frame) any^ {
    return local.receive(handle,frame.packet,frame_admission(credentials,peer,frame)^)^
}
pub pn receive_frames(handle, credentials, peer, frames, records=[]) any^ {
    if (not (frames is array) or len(frames)^ < 1 or len(frames)^ > 8192 or not batch_fits(frames)^)
        raise error("sync peer: invalid frame batch")
    // Authenticate the complete batch before entering one durable head transaction.
    let admitted = [for (frame in frames)
        {packet:frame.packet,admission:frame_admission(credentials,peer,frame)^}]
    return local.receive_batch(handle,admitted,records)^
}
pub pn bundle(handle, frames, ids) any^ {
    if (frames == null or len(ids)^ > 128) { return null }
    var records = []
    var bytes = 0
    for (id in ids) {
        let record = local.export_objects(handle,[id])^[0]
        bytes = bytes+len(record.box)^
        if (bytes > session.max_frame-32) { return null }
        records = [*records,record]
    }
    let body = {frames:frames,objects:records}
    if (len(binary(codec.encode({kind:'put_bundle',body:body})^))^ > session.max_frame-32 or
        len(binary(codec.encode({ok:true,body:body})^))^ > session.max_frame-32) { return null }
    return body
}
pub pn receive_bundle(handle, credentials, peer, body) any^ {
    v.fields(body,["frames","objects"])^
    if (not (body.objects is array) or len(body.objects)^ > 128)
        raise error("sync peer: invalid bundled objects")
    return receive_frames(handle,credentials,peer,body.frames,body.objects)^
}
pub fn frame_matches(frame, remote, index) any^ {
    if (remote.delta == true) raise error("sync peer: delta requires a complete atomic batch")
    if (frame.packet.body.checkpoint != remote.digest or frame.packet.body.index != index or
        frame.packet.body.total != remote.frames) raise error("sync peer: checkpoint response mismatch")
    return true
}
pub fn frames_match(frames, remote) any^ {
    if (not (frames is array) or len(frames)^ != remote.frames)
        raise error("sync peer: incomplete checkpoint batch")
    if (remote.delta == true) {
        if (codec.digest([for (frame in frames) frame.packet])^ != remote.digest)
            raise error("sync peer: delta response mismatch")
        return true
    }
    for (i in 0 to (remote.frames-1)) { frame_matches(frames[i],remote,i)^ }
    return true
}
pub fn bundle_objects(body, ids) any^ {
    v.fields(body,["frames","objects"])^
    if (not (body.objects is array) or sort([for (record in body.objects) record.id])^ != sort(ids)^)
        raise error("sync peer: bundled object inventory mismatch")
    return true
}

// outgoing data is pinned per connection; requests cannot silently switch a checkpoint mid-transfer.
pub pn request(handle, credentials, peer, outgoing, pending, command) any^ {
    v.fields(command,["kind","body"])^
    let body = command.body
    if (command.kind == 'head') {
        if (outgoing.peer_have == null) { v.fields(body,[])^ }
        else if (body != outgoing.peer_have) raise error("sync peer: changed round frontier")
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
    if (command.kind == 'frames') {
        v.fields(body,[])^
        if (outgoing.batch == null) raise error("sync peer: frame batch unavailable")
        return {body:outgoing.batch,pending:pending}
    }
    if (command.kind == 'bundle') {
        object_ids(body)^
        for (id in body) {
            if (not contains(outgoing.ids,id)^) raise error("sync peer: unadvertised bundled object")
        }
        return {body:bundle(handle,outgoing.batch,body)^,pending:pending}
    }
    if (command.kind == 'missing') {
        object_ids(body)^
        return {body:local.missing_objects(handle,body)^,pending:pending}
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
    if (command.kind == 'put_frames') {
        let applied = receive_frames(handle,credentials,peer,body)^
        return {body:applied.state.endpoint.replica.clock,pending:pending}
    }
    if (command.kind == 'put_bundle') {
        let applied = receive_bundle(handle,credentials,peer,body)^
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
