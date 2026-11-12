// Cloud publication is independent of peer receipt and filesystem installation.
import schema: .cloud_schema
import transport: .cloud_transport
import bundle: .cloud_bundle
import core: .core
import codec: .codec
import local: .local
import v: .schema
import document_state: .document_state
import catalog: .cloud_catalog
import maintenance: .cloud_maintenance_function
import retention_state: .maintenance_state
import inventory_pages: .cloud_inventory
import hex: lambda.security.tuf.metadata
import cache_state: .cache_state

pub fn provider(config) any^ => schema.digest([config.url,config.authority])^
fn sources(current) => if (current.cloud_sources == null) [] else current.cloud_sources
fn known_objects(current, provider) => {
    let found = [for (item in (if (current.cloud_objects == null) [] else current.cloud_objects) where item.provider == provider) item];
    if (len(found) == 0) [] else found[0].ids
}
pn install(handle, credentials, config, control, publication, verify=false) any^ {
    let bytes = transport.read(config,credentials,publication.bundle.key)^
    let payload = bundle.expand(config,credentials,publication,bundle.decode(credentials,publication,bytes)^)^
    let have = local.inventory(handle)^
    let deferred = cache_state.ids(local.status(handle)^.state)
    var downloaded = 0
    let locations = map([for (entry in payload.locations) (hex.hex(entry.id)^,entry.key)])
    let pending = [for (id in payload.ids where verify or (not contains(have,id)^ and not contains(deferred,id)^)) id]
    for (offset in range(0,len(pending)^,64)) {
        let ids = slice(pending,offset,offset+64)^
        let records = transport.read_many(config,credentials,[for (id in ids) locations[hex.hex(id)^]])^
        let decoded = [for (index in 0 to (len(ids)^-1)) {
            let record = codec.decode(records[index].bytes)^;
            if (record.id != ids[index]) raise error("sync cloud: object ID mismatch")
            record
        }]
        local.import_objects(handle,decoded)^
        downloaded = downloaded+len(decoded)^
    }
    // retain cold cache absences, but hydrate any chunk that the incoming live state now needs.
    let hot = local.incoming_hot_chunks(handle,payload.saved,if (payload.documents == null) [] else payload.documents)^
    let needed = [for (id in hot where contains(deferred,id)^ and not contains(have,id)^) id]
    for (offset in range(0,len(needed)^,64)) {
        let ids = slice(needed,offset,offset+64)^
        let records = transport.read_many(config,credentials,[for (id in ids) locations[hex.hex(id)^]])^
        let decoded = [for (index in 0 to (len(ids)^-1)) codec.decode(records[index].bytes)^]
        if ([for (record in decoded) record.id] != ids) raise error("sync cloud: hot chunk identities mismatch")
        local.import_objects(handle,decoded)^
        downloaded = downloaded+len(decoded)^
    }
    bundle.install_checkpoint(handle,credentials,schema.member(control,publication.actor,false)^,payload)^
    return {payload:payload,downloaded:downloaded}
}
pub pn pull(handle, credentials, config, verify=false) any^ {
    let remote = transport.call(config,credentials,"pull",{})^
    let control = schema.control(remote.control,config.authority)^
    if (control.space != credentials.space) raise error("sync cloud: wrong control space")
    let provider = provider(config)^
    local.record_cloud_control(handle,provider,control.generation,schema.digest(remote.control)^)^
    local.record_catalog_backup(handle,provider,remote.catalog_backup)^
    let current = local.status(handle)^.state
    let old_epochs = if (current.cloud_epochs == null) [] else current.cloud_epochs
    let seen_epoch = v.lookup(old_epochs,provider)^
    let epoch = if (remote.epoch == null) 0 else remote.epoch
    if (seen_epoch != null and epoch < seen_epoch.epoch) raise error("sync cloud: storage epoch rollback")
    var downloaded = 0
    var covered = []
    if (remote.checkpoint != null) {
        let checkpoint = maintenance.checkpoint(remote.checkpoint,control)^
        if (checkpoint.epoch != epoch) raise error("sync cloud: storage/checkpoint epoch mismatch")
        covered = checkpoint.covered
        let digest = schema.digest(remote.checkpoint)^
        if (seen_epoch != null and seen_epoch.epoch == epoch and seen_epoch.checkpoint != digest)
            raise error("sync cloud: maintenance checkpoint fork")
        if (seen_epoch == null or seen_epoch.epoch != epoch or verify) {
            let publication = schema.publication(checkpoint.publication,control)^
            let installed = install(handle,credentials,config,control,publication,verify)^
            downloaded = downloaded+installed.downloaded
            local.mutate(handle,pn (tx,k,current)=> {
                return {*:current,cloud_sources:[for (item in sources(current)^ where item.provider != provider) item],
                    cloud_objects:[for (item in (if (current.cloud_objects == null) [] else current.cloud_objects)
                        where item.provider != provider) item],
                    cloud_locations:[for (item in (if (current.cloud_locations == null) [] else current.cloud_locations)
                        where item.id != provider) item]}
            })^
            let payload = installed.payload
            local.record_cloud(handle,provider,"maintenance",epoch,digest,payload.saved,payload.ids,payload.documents,payload.locations)^
            local.mutate(handle,pn (tx,k,current)=> {
                return {*:current,cloud_epochs:[*[for (item in (if (current.cloud_epochs == null) [] else current.cloud_epochs)
                    where item.id != provider) item],{id:provider,epoch:epoch,checkpoint:digest}]}
            })^
        }
    } else if (epoch != 0) { raise error("sync cloud: missing maintenance checkpoint") }
    for (head in remote.heads) {
        let publication = schema.publication(head.publication,control)^
        if (head.actor != publication.actor) raise error("sync cloud: head actor mismatch")
        let cut = [for (item in covered where item.actor == head.actor) item]
        if (len(cut) != 0 and publication.sequence <= cut[0].sequence) { continue }
        let digest = schema.digest(head.publication)^
        let current = local.status(handle)^.state
        let seen = [for (item in sources(current)^ where item.provider == provider and item.actor == head.actor) item]
        if (len(seen) != 0) {
            if (publication.sequence < seen[0].sequence or
                (publication.sequence == seen[0].sequence and digest != seen[0].publication))
                raise error("sync cloud: head rollback/fork")
            if (publication.sequence == seen[0].sequence and not verify) { continue }
        }
        if (catalog.epoch(publication.bundle.key)^ != epoch) raise error("sync cloud: uncut head references retired storage")
        let installed = install(handle,credentials,config,control,publication,verify)^
        downloaded = downloaded+installed.downloaded
        let payload = installed.payload
        local.record_cloud(handle,provider,head.actor,publication.sequence,digest,payload.saved,payload.ids,
            if (payload.documents == null) [] else payload.documents,payload.locations)^
    }
    return {*:remote,downloaded:downloaded}
}
pub pn publish(handle, credentials, config, remote) any^ {
    let control = schema.control(remote.control,config.authority)^
    if (remote.phase != 'open') { return {published:false,uploaded:0,unchanged:false,paused:true} }
    let epoch = if (remote.epoch == null) 0 else remote.epoch
    let mine = [for (head in remote.heads where head.actor == credentials.device.id) head]
    let current = local.status(handle)^.state
    let ids = local.shared_inventory(handle)^
    let old = if (len(mine) == 0) null else mine[0]
    let previous = if (old == null) null else schema.publication(old.publication,control)^
    let known = known_objects(current,provider(config)^)^
    let own_receipt = [for (item in sources(current)^ where item.provider == provider(config)^ and
        item.actor == credentials.device.id and item.publication == (if (old == null) null else schema.digest(old.publication)^)) item]
    let same_inventory = if (previous == null) false else if (previous.version == 1)
        previous.inventory == schema.digest(ids)^ else len(own_receipt)^ == 1 and own_receipt[0].inventory == codec.digest(sort(ids)^)^
    if (previous != null and previous.clock == current.endpoint.replica.clock and same_inventory and catalog.epoch(previous.bundle.key)^ == epoch)
        { return {published:true,uploaded:0,unchanged:true} }
    if (remote.protocol == 2) { return publish_paged(handle,credentials,config,remote,current,ids,old,previous)^ }
    let captured = bundle.capture(handle,credentials,epoch)^
    var uploaded = 0
    for (id in captured.payload.ids) {
        if (not contains(known,id)^) {
            let record = local.export_objects(handle,[id])^[0]
            let encoded = codec.encode(record)^
            let stored = transport.ensure(config,credentials,transport.descriptor(bundle.object_key(id,epoch)^,encoded)^,encoded)^
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
pn publish_paged(handle, credentials, config, remote, current, ids, old, previous) any^ {
    let epoch = remote.epoch or 0
    let prior = local.cloud_locations(handle,provider(config)^,ids)^
    var physical = []
    var uploaded = 0
    for (entry in prior) {
        let key = if (entry.key == null) bundle.object_key(entry.id,epoch)^ else entry.key
        if (entry.key == null) {
            let record = local.export_objects(handle,[entry.id])^[0]
            let encoded = codec.encode(record)^
            let stored = transport.ensure(config,credentials,transport.descriptor(key,encoded)^,encoded)^
            if (stored.existing != null) {
                let existing = if (stored.existing.ok) codec.decode(stored.existing.body)^ else null
                if (existing == null or existing.id != entry.id) raise error("sync cloud: existing physical copy unavailable")
                local.import_objects(handle,[existing])^
            }
            if (stored.uploaded) { uploaded = uploaded+1 }
        }
        physical = [*physical,{id:entry.id,key:key}]
    }
    let objects = inventory_pages.stage(config,credentials,[for (entry in physical) entry.key],epoch)^
    let captured = bundle.capture(handle,credentials,epoch,ids,
        {root:if (objects == null) null else objects.root,locations:physical})^
    let descriptor = transport.descriptor(captured.key,captured.bytes)^
    let stored = transport.ensure(config,credentials,descriptor,captured.bytes)^
    if (stored.existing != null and (not stored.existing.ok or
        hash(stored.existing.body,'sha256')^ != hash(captured.bytes,'sha256')^)) raise error("sync cloud: bundle collision")
    let bundle_receipt = inventory_pages.stage(config,credentials,[captured.key],epoch)^
    let combined = inventory_pages.merge(config,credentials,[bundle_receipt,objects],epoch)^
    let publication = schema.signed(credentials.seed,"lambda-cloud-publication-1",{version:2,space:credentials.space,
        actor:credentials.device.id,sequence:if (previous == null) 1 else previous.sequence+1,
        previous:if (old == null) null else schema.digest(old.publication)^,clock:captured.payload.saved.frontier,
        bundle:descriptor,inventory:schema.digest(captured.payload.inventory)^,
        dependencies:combined.id,receipts:[combined.id]})^
    let result = transport.call(config,credentials,"commit",{publication:publication,expected:if (old == null) null else old.etag})^
    if (not result.published) { return {published:false,uploaded:uploaded,unchanged:false} }
    if (result.publication != publication) raise error("sync cloud: publication acknowledgment mismatch")
    local.record_cloud(handle,provider(config)^,credentials.device.id,publication.body.sequence,
        schema.digest(publication)^,captured.payload.saved,ids,captured.payload.documents,physical)^
    return {published:true,uploaded:uploaded,unchanged:false}
}
pub pn synchronize(handle, credentials, config) any^ {
    let pulled = pull(handle,credentials,config)^
    let published = publish(handle,credentials,config,pulled)^
    let current_state = local.status(handle)^.state
    return {*:published,downloaded:pulled.downloaded,frontier:current_state.endpoint.replica.clock,
        cloud_saved:covered(current_state,provider(config)^)^}
}
pub pn resolve_locations(credentials, config, remote, ids) any^ {
    let control = schema.control(remote.control,config.authority)^
    if (control.space != credentials.space) raise error("sync cloud: wrong locator space")
    let cut = if (remote.checkpoint == null) null else maintenance.checkpoint(remote.checkpoint,control)^
    let covered = if (cut == null) [] else cut.covered
    let selected = [*if (cut == null) [] else [cut.publication],
        *[for (head in remote.heads where not any([for (entry in covered)
            entry.actor == head.actor and entry.sequence >= head.publication.body.sequence])) head.publication]]
    var found = []
    for (signed in selected) {
        let publication = schema.publication(signed,control)^
        let payload = bundle.expand(config,credentials,publication,
            bundle.decode(credentials,publication,transport.read(config,credentials,publication.bundle.key)^)^)^
        found = [*found,*[for (entry in payload.locations where contains(ids,entry.id)^ and v.lookup(found,entry.id)^ == null) entry]]
        if (len(unique([for (entry in found) entry.id]))^ == len(ids)^) { break }
    }
    return found
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
    let retention = retention_state.get(current)
    let objects = known_objects(current,provider)^
    return workspace_covered and retention.generation == retention.manifest_generation and
        all([for (id in current.snapshots) contains(objects,id)^]) and
        (retention.manifest == null or contains(objects,retention.manifest)^) and
        (current.cache_root == null or contains(objects,current.cache_root)^) and all([for (record in document_state.all(current)^)
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
        versions = [*versions,{id:schema.digest(current)^,sequence:body.sequence,clock:body.clock,bundle:body.bundle,availability:if (catalog.epoch(body.bundle.key)^ < (remote.epoch or 0)) 'retired' else 'stored'}]
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
