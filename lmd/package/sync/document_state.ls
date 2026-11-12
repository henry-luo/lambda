// public editing state and private disk/import state have separate persistence boundaries.
import core: .core
import protocol: .protocol
import session: .session
import codec: .codec
import v: .schema
import text: .text
import mark: .mark
import conversion: .document_format
import hex: lambda.security.tuf.metadata

pub fn all(current) => if (current.documents == null) [] else current.documents
pub fn id(space, entry) any^ => hex.hex(codec.digest(["lambda-document-1",space,entry])^)^
pub fn get(current, document) any^ {
    let found = v.lookup(all(current),document)^
    if (found == null) raise error("sync document: unknown document")
    return found
}
pub fn replace(current, record) any^ => {*:current,documents:sort(
    [*[for (item in all(current) where item.id != record.id) item],record],(item)=>item.id)^}
pub fn request(record, kind, id) any^ {
    if (id == null) { return null }
    if (not v.actor(id)^) raise error("sync document: invalid editor request ID")
    return v.lookup(if (record.requests == null) [] else record.requests,[kind,id])^
}
pub fn record_request(record, kind, id, result) any^ => if (id == null) record
    else {*:record,requests:[*if (record.requests == null) [] else record.requests,{id:[kind,id],*:result}]}
pub fn coverage(record) any^ {
    let accepted = protocol.status(record.endpoint)
    let cloud = if (record.cloud_roots == null) [] else record.cloud_roots
    let peers = if (record.peer_roots == null) [] else record.peer_roots
    return {*:accepted,local:accepted.local and record.export_id != null,
        cloud:accepted.cloud and any([for (root in cloud) root.object == record.export_id]),
        peer:accepted.peer and any([for (root in peers) root.object == record.export_id])}
}

pub fn create(space, entry, profile, encoding, raw) any^ {
    let document = id(space,entry)^
    let value = conversion.parse_raw(raw,profile,encoding)^
    let actor = "seed:" ++ hex.hex(codec.digest([document,profile,encoding,raw])^)^
    let replica = if (profile == 'text') text.from_text(document,actor,value)^
        else mark.from_value(document,actor,value)^
    conversion.verify_raw(replica,encoding,raw)^
    let opened = session.start(replica,raw)^
    return {id:document,entry:entry,format:encoding,origin:opened.base,
        endpoint:protocol.start(replica)^,session:opened,disk_hash:hash(raw,'sha256')^,
        opened:true,prepared:null,export_id:null}
}
pub fn public_state(record) any^ => {version:1,id:record.id,entry:record.entry,format:record.format,
    origin:record.origin,live:core.snapshot(record.endpoint.replica)^,versions:sort(record.session.versions,(saved)=>saved.id)^}

pub fn validate_public(value, space) any^ {
    v.fields(value,["version","id","entry","format","origin","live","versions"])^
    if (value.version != 1 or not v.id(value.entry)^ or value.id != id(space,value.entry)^ or
        not (value.versions is array) or len(value.versions) == 0 or
        len(unique([for (saved in value.versions) saved.id])) != len(value.versions))
        raise error("sync document: invalid public state")
    let replica = core.restore(value.live)^
    if (replica.document != value.id or not contains(['text','mark'],replica.profile)^)
        raise error("sync document: wrong document identity/profile")
    conversion.validate(replica.profile,value.format)^
    for (saved in [value.origin,*value.versions]) {
        session.validate_version(saved)^
        conversion.verify_raw(saved.replica,value.format,saved.raw)^
        if (not core.dominates(replica.clock,saved.frontier)^) raise error("sync document: version exceeds live frontier")
        core.merge(replica,saved.replica)^
    }
    return true
}
pub fn validate(record, space) any^ {
    protocol.validate_endpoint(record.endpoint)^
    if (core.compact(record.endpoint.replica) != core.compact(record.session.live))
        raise error("sync document: endpoint/session mismatch")
    validate_public(public_state(record)^,space)^
    session.validate_version(record.session.base)^
    return true
}
pub fn update(record, endpoint) any^ => {*:record,endpoint:endpoint,
    session:session.update(record.session,endpoint.replica)^}

pub fn merge_public(record, incoming, space) any^ {
    validate_public(incoming,space)^
    if (record == null) {
        let restored = core.restore(incoming.live)^
        let initial = session.start(incoming.origin.replica,incoming.origin.raw)^
        let base = {id:incoming.id,entry:incoming.entry,format:incoming.format,origin:incoming.origin,
            endpoint:protocol.start(restored)^,session:session.update(initial,restored)^,
            disk_hash:null,opened:false,prepared:null,export_id:null}
        return merge_public(base,incoming,space)^
    }
    if (record.entry != incoming.entry or record.format != incoming.format or record.origin != incoming.origin)
        raise error("sync document: incompatible initialization; both branches retained")
    var merged = session.update(record.session,core.restore(incoming.live)^)^
    var index = 0
    while (index < len(incoming.versions)^) {
        merged = session.receive_version(merged,incoming.versions[index])^
        index = index+1
    }
    return {*:record,endpoint:protocol.replace_replica(record.endpoint,merged.live)^,session:merged}
}
pub fn compact(record) any^ {
    let endpoint = protocol.compact(record.endpoint)^
    return {*:record,endpoint:endpoint,session:session.compact(session.update(record.session,endpoint.replica)^)}
}
