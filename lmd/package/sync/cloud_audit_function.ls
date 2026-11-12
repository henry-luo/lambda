// discovery never chooses a latest head; an owner audits a frozen catalog before re-enabling cleanup.
import catalog: .cloud_catalog
import schema: .cloud_schema
import codec: .codec
import pages: .metadata_pages
import retirements: .cloud_retirements
import v: .schema

pub fn backup(value, id, control) any^ {
    if (schema.digest(value)^ != id) raise error("sync audit: backup digest mismatch")
    let candidate = catalog.normalize(value)^
    if (candidate.phase != 'open' or not (candidate.heads is array) or len(candidate.heads)^ > 64)
        raise error("sync audit: invalid backup catalog")
    let cut = if (candidate.checkpoint == null) null else schema.maintenance_checkpoint(candidate.checkpoint,control)^
    if ((cut == null and candidate.epoch != 0) or (cut != null and cut.epoch != candidate.epoch))
        raise error("sync audit: backup checkpoint epoch mismatch")
    for (head in candidate.heads) {
        let published = schema.publication(head.publication,control)^
        if (head.actor != published.actor) raise error("sync audit: backup actor mismatch")
    }
    return {id:id,epoch:candidate.epoch,version:candidate.version,heads:schema.catalog_heads(candidate)^,
        checkpoint:if (candidate.checkpoint == null) null else schema.digest(candidate.checkpoint)^}
}
pub fn summary(value) => if (value.audit == null) null else
    {id:value.audit.id,stage:value.audit.stage,complete:value.audit.stage == 'complete',
     checked:value.audit.checked,inventory:value.audit.inventory,quarantine:value.audit.quarantine,
     next_epoch:value.next_epoch,heads:value.audit.heads,base:value.audit.base}
pub pn handle(store, prefix, control, envelope) any^ {
    if (envelope.actor != control.owner.id) raise error("sync audit: owner authority required")
    let body = envelope.body
    let kind = envelope.kind
    if (kind == "backup_discover") {
        v.record(body,[],["cursor","limit"])^
        let limit = body.limit or 4
        if (not v.natural(limit)^ or limit < 1 or limit > 16) raise error("sync audit: discovery budget")
        let found = store.enumerate(prefix ++ "catalogs/",body.cursor,limit)^
        var entries = []
        for (entry in found.entries) {
            let id = slice(entry.key,len(prefix ++ "catalogs/")^ )^
            let candidate = if (not schema.id(id)^) error("invalid backup name") else
                backup(codec.decode(store.get(entry.key)^.body)^,id,control) ^ { ^ }
            entries = [*entries,if (candidate is error) {id:id,valid:false} else {*:candidate,valid:true}]
        }
        return {entries:entries,next:found.next}
    }
    let current = catalog.load(store,prefix,control)^
    let value = current.value
    if (kind == "audit_begin") {
        v.fields(body,["expected"])^
        if (value.phase != 'open' or current.etag != body.expected or len(value.uploads)^ != 0)
            raise error("sync audit: stale catalog or pending publication")
        if (value.maintenance != null and value.maintenance.aborted != true) {
            let old = if (value.version == 2) retirements.summary(store,prefix,value.maintenance)^ else
                {next:value.maintenance.next,total:len(value.maintenance.old_keys)^}
            if (not value.maintenance.committed or old.next != old.total)
                raise error("sync audit: finish the previous maintenance job")
        }
        let root = if (value.version == 2) value.objects_root else
            pages.build(store,prefix,[for (key in value.objects) {id:key,value:null}])^
        let audit = {id:schema.digest(["audit",uuid()^,current.etag])^,base:current.etag,
            heads:schema.catalog_heads(value)^,stage:'allocations',cursor:null,checked:0,
            inventory:null,quarantine:value.quarantine_root,max_epoch:value.epoch}
        let next_epoch = max(catalog.available_epoch(store,prefix,value)^,value.epoch+2)
        let changed = catalog.write(store,prefix,current,{*:value,version:2,phase:'audit',legacy:true,
            objects:[],objects_root:root,next_epoch:next_epoch,audit:audit})^
        return {started:changed.written,audit:summary({audit:audit,next_epoch:next_epoch})}
    }
    if (kind == "audit_status") { v.fields(body,[])^; return summary(value) }
    if (value.phase != 'audit' or value.audit == null or body.id != value.audit.id)
        raise error("sync audit: unknown frozen audit")
    if (kind == "audit_abort") {
        v.fields(body,["id"])^
        let changed = catalog.write(store,prefix,current,{*:value,phase:'open',audit:null,legacy:true})^
        return {aborted:changed.written}
    }
    if (kind != "audit_step") raise error("sync audit: unsupported request")
    v.fields(body,["id","budget"])^
    if (not v.natural(body.budget)^ or body.budget < 1 or body.budget > 64) raise error("sync audit: invalid step budget")
    let task = value.audit
    if (task.stage == 'complete') { return {advanced:true,audit:summary(value)} }
    let stage_prefix = if (task.stage == 'allocations') "allocations/" else if (task.stage == 'backups') "catalogs/"
        else if (task.stage == 'objects') "objects/" else "bundles/"
    let found = store.enumerate(prefix ++ stage_prefix,task.cursor,body.budget)^
    var inventory = task.inventory
    var quarantine = task.quarantine
    var high = task.max_epoch
    for (entry in found.entries) {
        let key = slice(entry.key,len(prefix)^)^
        if (task.stage == 'allocations') {
            let allocated = codec.decode(store.get(entry.key)^.body)^
            v.fields(allocated,["version","epoch","id"])^
            if (allocated.version != 1 or not v.natural(allocated.epoch)^ or not schema.id(allocated.id)^)
                raise error("sync audit: invalid generation claim")
            high = max(high,allocated.epoch)
        } else if (task.stage == 'backups') {
            let id = slice(key,9)^
            let candidate = backup(codec.decode(store.get(entry.key)^.body)^,id,control) ^ { null }
            if (candidate != null) { high = max(high,candidate.epoch+1) }
        } else {
            if (not schema.data_key(key)^) raise error("sync audit: unsupported data path requires operator review")
            high = max(high,catalog.epoch(key)^)
            inventory = pages.upsert(store,prefix,inventory,key,{etag:entry.etag,size:entry.size})^
            if (not catalog.registered(store,prefix,value,key)^) {
                quarantine = pages.upsert(store,prefix,quarantine,key,{etag:entry.etag,size:entry.size})^
            }
        }
    }
    let stage = if (found.next != null) task.stage else if (task.stage == 'allocations') 'backups'
        else if (task.stage == 'backups') 'objects' else if (task.stage == 'objects') 'bundles' else 'complete'
    let updated = {*:task,stage:stage,cursor:found.next,checked:task.checked+len(found.entries)^,
        inventory:inventory,quarantine:quarantine,max_epoch:high}
    let next_epoch = max(value.next_epoch,high+1)
    let changed = catalog.write(store,prefix,current,{*:value,next_epoch:next_epoch,audit:updated})^
    return {advanced:changed.written,audit:summary({audit:if (changed.written) updated else task,
        next_epoch:if (changed.written) next_epoch else value.next_epoch})}
}
