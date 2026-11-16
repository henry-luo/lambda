// Negotiated warm exchanges carry signed deltas and their immutable dependency closure.
import exchange: .peer_exchange
import local: .local
import owner: .task_owner
import documents: .document_state
import maintenance: .maintenance_state
import protocol: .protocol
import workspace: .workspace
import core: .core
import codec: .codec
import session: .peer_session
import v: .schema

fn eligible(current) bool^ => len(documents.all(current))^ == 0 and
    maintenance.get(current).manifest == null and current.cache_root == null and
    len(current.endpoint.replica.pending)^ == 0 and len(current.endpoint.transactions)^ == 0

pub fn token(data) any^ {
    let value = {clock:data.summary.clock,epoch:data.summary.epoch,documents:[],retention:null,cache:null}
    return {*:value,digest:codec.digest(value)^}
}
pub pn prepare(handle, credentials, have) any^ {
    if (handle.keys.owner != null and handle.keys.owner != self()) {
        return owner.cached(handle.keys.owner,pn (cache)=> {
            return prepare({*:handle,keys:{*:handle.keys,cached_head:cache}},credentials,have)^
        })^
    }
    let current = local.status(handle)^.state
    if (have == null or not eligible(current)^ or have.epoch != current.endpoint.replica.epoch or
        not core.dominates(have.body.clock,current.endpoint.replica.floor)^) { return null }
    let outgoing = exchange.offer(handle,credentials,true,true,have,false)^
    if (outgoing.summary.delta != true) { return null }
    let ids = local.delta_objects(handle,outgoing.batch,exchange.page_size,session.max_frame div 4)^
    if (ids == null) { return null }
    let bundle = exchange.bundle(handle,outgoing.batch,ids)^
    if (bundle == null) { return null }
    let data = {have:exchange.have_frontier(credentials.space,outgoing.token)^,
        summary:{*:outgoing.summary,objects:len(ids)^},bundle:bundle,
        ack:{epoch:outgoing.token.epoch,clock:outgoing.token.clock}}
    if (max(len(binary(codec.encode({kind:'change',body:data})^))^,
        len(binary(codec.encode({ok:true,body:{accepted:true,exchange:data,clock:data.summary.clock}})^))^) > session.max_frame-32)
        { return null }
    return {data:data,token:outgoing.token,repository_cache:outgoing.repository_cache}
}

pub pn accept(handle, credentials, peer, data) any^ {
    if (handle.keys.owner != null and handle.keys.owner != self()) {
        return owner.cached(handle.keys.owner,pn (cache)=> {
            return accept({*:handle,keys:{*:handle.keys,cached_head:cache}},credentials,peer,data)^
        })^
    }
    v.fields(data,["have","summary","bundle","ack"])^
    exchange.summary(data.summary,credentials.space)^
    v.fields(data.ack,["epoch","clock"])^
    if (data.summary.delta != true or data.ack.epoch != data.summary.epoch or
        data.have.epoch != data.ack.epoch or data.ack.clock != data.summary.clock or
        data.have.body.clock != data.ack.clock or len(data.summary.documents)^ != 0 or
        data.summary.retention != null or data.summary.cache != null)
        raise error("sync fast: inconsistent frontier or unsupported controls")
    let current = local.status(handle)^.state
    // Header/HAVE validation also rejects foreign roots, spaces, profiles and schemas.
    protocol.reply(current.endpoint.replica,data.have,workspace.model)^
    if (not eligible(current)^ or data.summary.epoch != current.endpoint.replica.epoch) { return null }
    exchange.frames_match(data.bundle.frames,data.summary)^
    let ids = [for (record in data.bundle.objects) record.id]
    exchange.object_ids(ids)^
    if (len(ids)^ != data.summary.objects) raise error("sync fast: object count mismatch")
    return exchange.receive_bundle(handle,credentials,peer,data.bundle,
        {peer:peer.id,clock:data.ack.clock,release:ids})^
}
