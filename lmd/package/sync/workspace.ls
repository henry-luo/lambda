// public namespace facade; filesystem and network effects remain host procedures.
import c: .core
import v: .schema
import codec: .codec
import blob: .blob
import tree: .workspace_tree
import rules: .workspace_rules
import p: .protocol

pub let root = c.root
pub let model = ["workspace",1]
pub let revision = rules.revision

pub fn empty(id) => c.empty(id,'workspace','workspace',null)
pub fn start(id) any^ => p.start(empty(id),model)^
pub fn transact(endpoint, actor, commands, retain_undo=true) any^ => p.local_change(endpoint,actor,commands,8388608,retain_undo)^
pub fn have(endpoint) any^ => p.have(endpoint.replica,model)^

pub fn read(replica) any^ {
    c.validate(replica)^
    return tree.project(tree.events(replica)^)^
}

pub fn entry(replica, id) any^ {
    let found = v.lookup(read(replica)^.entries,id)^
    if (found == null) raise error("sync workspace: unknown entry")
    return found
}

pub fn created(edited) any^ => [for (atom in edited.endpoint.replica.atoms where
    not tree.seen(edited.undo.batch.base,atom.id)^ and atom.value.event.action == 'create')
    {id:atom.id,name:atom.value.event.name,type:atom.value.event.type}]

pub fn path(replica, id, materialized=false) any^ => tree.path(read(replica)^.entries,id,materialized)^

pub fn content(bytes, media_type="application/octet-stream") any^ {
    let payload = if (bytes is string) binary(bytes)^ else bytes
    if (payload != null and not (payload is binary)) raise error("sync workspace: content requires bytes or text")
    let size = if (payload == null) 0 else len(payload)
    let digest = hash(payload,'sha256')^
    let manifest = codec.digest(["lambda-single-blob-1",size,digest])^
    return {reference:blob.reference(manifest,size,digest,media_type)^,object:{id:manifest,bytes:payload}}
}

pub fn objects(a, b) any^ {
    if (not (a is array) or not (b is array)) raise error("sync workspace: expected object catalogs")
    var result = a
    var i = 0
    while (i < len(b)) {
        let item = b[i]
        v.fields(item,["id","bytes"])^
        if (not (item.id is binary) or len(item.id)^ != 32 or (item.bytes != null and not (item.bytes is binary)))
            raise error("sync workspace: invalid object")
        let previous = v.lookup(result,item.id)^
        if (previous != null and previous != item) raise error("sync workspace: immutable object ID reused")
        if (previous == null) { result = [*result,item] }
        i = i+1
    }
    return sort(result,(item)=>item.id)^
}

pub fn payload(reference, objects) any^ {
    let stored = v.lookup(objects,reference.manifest)^
    if (stored == null or not blob.verify(reference,stored.bytes)^) raise error("sync workspace: missing or corrupt content")
    return stored.bytes
}

pub fn document(saved, schema=null) any^ {
    let replica = c.restore(saved)^
    let link = {document:replica.document,profile:replica.profile,epoch:replica.epoch,
        schema:if (schema != null) schema else if (replica.profile == 'workspace') model else ["mark",1],
        frontier:replica.clock,checkpoint:codec.digest(p.checkpoint(replica))^}
    rules.document(link)^
    return link
}

pub fn roots(replica) any^ => {contents:blob.required(replica)^,checkpoints:blob.checkpoints(replica)^,
    revisions:unique([for (event in tree.events(replica)^ where event.action == 'publish' or
        (event.action == 'create' and event.type == 'file')) event.revision.id])}
