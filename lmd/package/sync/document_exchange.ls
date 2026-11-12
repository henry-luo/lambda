// full-space device grants attest relayed deltas, matching the existing checkpoint trust profile.
import local: .local
import model: .document_state
import protocol: .protocol
import core: .core
import codec: .codec
import v: .schema
import crypto: lambda.io.crypto

pub fn descriptor(record) any^ => {id:record.id,object:record.export_id,origin:record.origin.id,
    have:protocol.have(record.endpoint.replica,record.endpoint.schema)^,
    versions:codec.digest(sort([for (saved in record.session.versions) saved.id]))^}
pub fn catalog(records) any^ => [for (record in records) descriptor(record)^]
pub fn validate_catalog(values) any^ {
    if (not (values is array) or len(values) > 64 or len(unique([for (item in values) item.id])) != len(values))
        raise error("sync document: invalid peer catalog")
    var index = 0
    while (index < len(values)) {
        let item = values[index]
        v.fields(item,["id","object","origin","have","versions"])^
        if (not v.actor(item.id)^ or item.have.document != item.id or item.have.kind != 'have' or
            not contains(['text','mark'],item.have.profile)^ or
            not all([for (digest in [item.object,item.origin,item.versions]) digest is binary and len(digest) == 32]))
            raise error("sync document: invalid peer descriptor")
        v.clock(item.have.body.clock)^
        index = index+1
    }
    return true
}
pub pn need(handle, remote) any^ {
    validate_catalog([remote])^
    let record = v.lookup(model.all(local.status(handle)^.state),remote.id)^
    if (record == null) { return {root:true,have:null} }
    if (record.origin.id != remote.origin) raise error("sync document: incompatible initialization; peer branch retained")
    let own = descriptor(record)^
    return {root:own.versions != remote.versions,have:own.have}
}
fn signed_bytes(space, body) any^ => codec.encode(["lambda-document-relay-1",space,body])^
pub fn reply(record, credentials, have, index=0) any^ {
    let next = protocol.next(record.endpoint,have)^
    let packets = if (next.kind == 'checkpoint_required') protocol.frames(record.endpoint.replica,record.endpoint.schema,65536)^ else [next]
    if (not v.natural(index)^ or index >= len(packets)) raise error("sync document: invalid delta/frame index")
    let body = {id:record.id,origin:record.origin.id,packet:packets[index],frontier:record.endpoint.replica.clock}
    return {body:body,signature:crypto.sign(credentials.seed,signed_bytes(credentials.space,body)^)^}
}
pub pn receive_packet(handle, credentials, peer, envelope) any^ {
    v.fields(envelope,["body","signature"])^
    if (not verify_signature(peer.public_key,signed_bytes(credentials.space,envelope.body)^,
        envelope.signature,'ed25519')^) raise error("sync document: relay attestation rejected")
    let body = envelope.body
    v.fields(body,["id","origin","packet","frontier"])^
    let result = local.mutate(handle,pn (tx,k,current)=> {
        let record = model.get(current,body.id)^
        if (record.origin.id != body.origin) raise error("sync document: initialization mismatch")
        let packet = body.packet
        let admission = if (packet.kind == 'checkpoint_frame') {actors:[],checkpoints:[packet.body.checkpoint]}
            else {actors:unique([for (op in packet.body.operations) op.id[0]]),checkpoints:[]}
        let endpoint = protocol.accept(record.endpoint,codec.encode(packet)^,admission)^
        return model.replace(current,model.update(record,endpoint)^)^
    })^
    let record = model.get(result.state,body.id)^
    return protocol.have(record.endpoint.replica,record.endpoint.schema)^
}
pub pn acknowledge(handle, document, peer, clock, object_id=null) any^ {
    return local.mutate(handle,pn (tx,k,current)=> {
        let record = model.get(current,document)^
        let old = if (record.peer_roots == null) [] else record.peer_roots
        return model.replace(current,{*:record,endpoint:protocol.acknowledge(record.endpoint,peer,clock)^,
            peer_roots:[*[for (item in old where item.id != peer) item],{id:peer,object:object_id}]})^
    })^
}
