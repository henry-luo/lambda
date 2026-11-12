// Cloud publication is independent of peer receipt and filesystem installation.
import schema: .cloud_schema
import transport: .cloud_transport
import bundle: .cloud_bundle
import core: .core
import codec: .codec
import local: .local
import v: .schema
import document_state: .document_state

pub fn provider(config) any^ => schema.digest([config.url,config.authority])^
fn sources(current) => if (current.cloud_sources == null) [] else current.cloud_sources
fn known_objects(current, provider) => {
    let found = [for (item in (if (current.cloud_objects == null) [] else current.cloud_objects) where item.provider == provider) item];
    if (len(found) == 0) [] else found[0].ids
}
pub pn pull(handle, credentials, config) any^ {
    let remote = transport.call(config,credentials,"pull",{})^
    let control = schema.control(remote.control,config.authority)^
    if (control.space != credentials.space) raise error("sync cloud: wrong control space")
    let provider = provider(config)^
    local.record_cloud_control(handle,provider,control.generation,schema.digest(remote.control)^)^
    var downloaded = 0
    for (head in remote.heads) {
        let publication = schema.publication(head.publication,control)^
        if (head.actor != publication.actor) raise error("sync cloud: head actor mismatch")
        let digest = schema.digest(head.publication)^
        let current = local.status(handle)^.state
        let seen = [for (item in sources(current)^ where item.provider == provider and item.actor == head.actor) item]
        if (len(seen) != 0) {
            if (publication.sequence < seen[0].sequence or
                (publication.sequence == seen[0].sequence and digest != seen[0].publication))
                raise error("sync cloud: head rollback/fork")
            if (publication.sequence == seen[0].sequence) { continue }
        }
        let bytes = transport.read(config,credentials,publication.bundle.key)^
        let payload = bundle.decode(credentials,publication,bytes)^
        let have = local.inventory(handle)^
        for (id in payload.ids) {
            if (not contains(have,id)^) {
                let record = codec.decode(transport.read(config,credentials,bundle.object_key(id)^)^)^
                if (record.id != id) raise error("sync cloud: object ID mismatch")
                local.import_objects(handle,[record])^
                downloaded = downloaded+1
            }
        }
        bundle.install_checkpoint(handle,credentials,schema.member(control,head.actor,false)^,payload)^
        local.record_cloud(handle,provider,head.actor,publication.sequence,digest,payload.saved,payload.ids,
            if (payload.documents == null) [] else payload.documents)^
    }
    return {*:remote,downloaded:downloaded}
}
pub pn publish(handle, credentials, config, remote) any^ {
    let control = schema.control(remote.control,config.authority)^
    let mine = [for (head in remote.heads where head.actor == credentials.device.id) head]
    let current = local.status(handle)^.state
    let ids = local.inventory(handle)^
    let old = if (len(mine) == 0) null else mine[0]
    let previous = if (old == null) null else schema.publication(old.publication,control)^
    if (previous != null and previous.clock == current.endpoint.replica.clock and previous.inventory == schema.digest(ids)^)
        { return {published:true,uploaded:0,unchanged:true} }
    let captured = bundle.capture(handle,credentials)^
    var uploaded = 0
    let known = known_objects(current,provider(config)^)^
    for (id in captured.payload.ids) {
        if (not contains(known,id)^) {
            let record = local.export_objects(handle,[id])^[0]
            let encoded = codec.encode(record)^
            let stored = transport.ensure(config,credentials,transport.descriptor(bundle.object_key(id)^,encoded)^,encoded)^
            if (stored.existing != null) {
                if (not stored.existing.ok) raise error("sync cloud: existing object unavailable")
                let existing = codec.decode(stored.existing.body)^
                if (existing.id != id) raise error("sync cloud: immutable object mismatch")
                local.import_objects(handle,[existing])^
            }
            if (stored.uploaded) { uploaded = uploaded+1 }
        }
    }
    let descriptor = transport.descriptor(captured.key,captured.bytes)^
    let stored = transport.ensure(config,credentials,descriptor,captured.bytes)^
    if (stored.existing != null and (not stored.existing.ok or
        hash(stored.existing.body,'sha256')^ != hash(captured.bytes,'sha256')^)) raise error("sync cloud: bundle collision")
    let dependencies = bundle.dependencies(captured.payload.ids,captured.key)^
    var receipts = []
    for (i in range(0,len(dependencies),64)) {
        let receipt = transport.call(config,credentials,"stage",{keys:slice(dependencies,i,i+64)^})^
        receipts = [*receipts,receipt.id]
    }
    let publication = schema.signed(credentials.seed,"lambda-cloud-publication-1",{version:1,space:credentials.space,
        actor:credentials.device.id,sequence:if (previous == null) 1 else previous.sequence+1,
        previous:if (old == null) null else schema.digest(old.publication)^,clock:captured.payload.saved.frontier,
        bundle:descriptor,inventory:schema.digest(captured.payload.ids)^,
        dependencies:schema.dependency_digest(dependencies)^,receipts:receipts})^
    let result = transport.call(config,credentials,"commit",{publication:publication,expected:if (old == null) null else old.etag})^
    if (not result.published) { return {published:false,uploaded:uploaded,unchanged:false} }
    if (result.publication != publication) raise error("sync cloud: publication acknowledgment mismatch")
    local.record_cloud(handle,provider(config)^,credentials.device.id,publication.body.sequence,
        schema.digest(publication)^,captured.payload.saved,captured.payload.ids,captured.payload.documents)^
    return {published:true,uploaded:uploaded,unchanged:false}
}
pub pn synchronize(handle, credentials, config) any^ {
    let pulled = pull(handle,credentials,config)^
    let published = publish(handle,credentials,config,pulled)^
    let current_state = local.status(handle)^.state
    return {*:published,downloaded:pulled.downloaded,frontier:current_state.endpoint.replica.clock,
        cloud_saved:covered(current_state,provider(config)^)^}
}

pub fn covered(current, provider) bool^ {
    var frontier = []
    let selected = [for (item in sources(current)^ where item.provider == provider) item]
    var i = 0
    while (i < len(selected)) {
        frontier = core.clock_join(frontier,selected[i].clock)^
        i = i+1
    }
    let workspace_covered = core.dominates(frontier,current.endpoint.replica.clock)^
    let roots = [for (item in selected where item.documents != null) for (root in item.documents) root]
    return workspace_covered and all([for (record in document_state.all(current)^)
        any([for (root in roots) root.id == record.id and root.object == record.export_id])])
}

pub pn history(credentials, config, actor, start=null, limit=32) any^ {
    if (not schema.id(actor) or not v.natural(limit)^ or limit < 1 or limit > 128)
        raise error("sync cloud: invalid history request")
    let remote = transport.call(config,credentials,"pull",{})^
    let control = schema.control(remote.control,config.authority)^
    let heads = [for (head in remote.heads where head.actor == actor) head]
    if (len(heads) == 0) { return {versions:[],next:null} }
    var current = if (start == null) heads[0].publication else codec.decode(
        transport.read(config,credentials,"publications/" ++ start)^)^
    if (start != null and schema.digest(current)^ != start) raise error("sync cloud: historical digest mismatch")
    var versions = []
    var i = 0
    while (current != null and i < limit) {
        let body = schema.publication(current,control)^
        if (body.actor != actor) raise error("sync cloud: historical actor mismatch")
        versions = [*versions,{id:schema.digest(current)^,sequence:body.sequence,clock:body.clock,bundle:body.bundle}]
        if (body.previous == null) {
            if (body.sequence != 1) raise error("sync cloud: broken history root")
            return {versions:versions,next:null}
        }
        if (i+1 == limit) { return {versions:versions,next:body.previous} }
        let previous = codec.decode(transport.read(config,credentials,"publications/" ++ body.previous)^)^
        let before = schema.publication(previous,control)^
        if (schema.digest(previous)^ != body.previous or before.sequence+1 != body.sequence or
            not core.dominates(body.clock,before.clock)^) raise error("sync cloud: broken publication history")
        current = previous
        i = i+1
    }
    return {versions:versions,next:null}
}
