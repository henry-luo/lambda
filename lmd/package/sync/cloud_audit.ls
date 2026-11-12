// resumable owner verification binds observed ciphertext and a complete retained graph to the frozen cut.
import local: .local
import cloud: .cloud
import bundle: .cloud_bundle
import transport: .cloud_transport
import maintenance: .cloud_maintenance
import schema: .cloud_schema
import pages: .metadata_pages
import inventory: .cloud_inventory
import repo: .repository
import codec: .codec
import hex: lambda.security.tuf.metadata
import v: .schema

fn tasks(current) => current.cloud_audits or []
pub pn status(handle, config) any^ {
    return v.lookup(tasks(local.status(handle)^.state)^,cloud.provider(config)^)^
}
pn save(handle, config, task) any^ {
    let provider = cloud.provider(config)^
    local.mutate(handle,pn (tx,k,current)=> {
        return {*:current,cloud_audits:[*[for (entry in tasks(current)^ where entry.id != provider) entry],
            *if (task == null) [] else [{*:task,id:provider}]]}
    })^
    return task
}
pub pn begin(handle, credentials, config) any^ {
    if (credentials.device.id != config.authority) raise error("sync audit: owner key required")
    if (status(handle,config)^ != null) raise error("sync audit: resume or abort the local audit")
    if (maintenance.status(handle,config)^ != null) raise error("sync audit: finish or abort local maintenance")
    // preserve external/offline changes before any remote checkpoint is merged.
    local.scan(handle)^
    let remote = transport.call(config,credentials,"pull",{})^
    let control = schema.control(remote.control,config.authority)^
    if (control.space != credentials.space) raise error("sync audit: wrong space")
    let begun = if (remote.phase == 'audit' and remote.audit != null) {started:true,audit:remote.audit} else
        transport.call(config,credentials,"audit_begin",{expected:remote.catalog_etag})^
    if (not begun.started) raise error("sync audit: catalog contention; retry")
    return save(handle,config,{audit:begun.audit.id,root:null,cursor:null,checked:0,bad:0,
        samples:[],complete:false,attestation:null})^
}
pub pn scan(handle, credentials, config, budget=32) any^ {
    let task = status(handle,config)^
    if (task == null) raise error("sync audit: no local audit")
    return transport.call(config,credentials,"audit_step",{id:task.audit,budget:budget})^
}
pub fn check(keys, credentials, control, physical_key, bytes) any^ {
    if (starts_with(physical_key,"objects/")^) {
        let record = codec.decode(bytes)^
        if (not ends_with(physical_key,"/" ++ hex.hex(record.id)^)^ or
            contains(["state","maintenance","locator"],record.kind)^)
            raise error("sync audit: unsupported object identity or private kind")
        repo.validate_record(keys,record)^
    } else { bundle.inspect(credentials,control,physical_key,bytes)^ }
    return true
}
pn validate_entry(handle, credentials, config, control, entry) any^ {
    let bytes = transport.read_many(config,credentials,[entry.id],[entry.value.etag])^[0].bytes
    if (len(binary(bytes)^)^ != entry.value.size) raise error("sync audit: observed size changed")
    return check(handle.keys,credentials,control,entry.id,bytes)^
}
pub pn step(handle, credentials, config, budget=8) any^ {
    if (not v.natural(budget)^ or budget < 1 or budget > 8) raise error("sync audit: verification budget is 1..8")
    let task = status(handle,config)^
    if (task == null) raise error("sync audit: no local audit")
    let remote = transport.call(config,credentials,"pull",{})^
    let control = schema.control(remote.control,config.authority)^
    let audit = remote.audit
    if (remote.phase != 'audit' or audit == null or audit.id != task.audit or not audit.complete)
        raise error("sync audit: finish frozen inventory discovery first")
    if (task.attestation != null) { return task }
    if (task.root != null and task.root != audit.inventory) raise error("sync audit: frozen root changed")
    let part = if (task.complete) {entries:[],next:null} else
        pages.page(inventory.reader(config,credentials),"",audit.inventory,task.cursor,budget)^
    var checked = task.checked
    var bad = task.bad
    var samples = task.samples
    for (entry in part.entries) {
        let validated: any^ = validate_entry(handle,credentials,config,control,entry)
        checked = checked+1
        if (validated is error) {
            bad = bad+1
            if (len(samples)^ < 32) { samples = [*samples,entry.id] }
        }
    }
    let updated = {*:task,root:audit.inventory,cursor:part.next,checked:checked,bad:bad,
        samples:samples,complete:part.next == null}
    save(handle,config,updated)^
    if (not updated.complete or bad != 0) { return updated }
    // force re-read of published dependencies, even when this device has cached valid copies.
    cloud.pull(handle,credentials,config,true)^
    local.verify(handle)^
    if (checked != (if (audit.inventory == null) 0 else audit.inventory.count))
        raise error("sync audit: incomplete object verification")
    let report = schema.signed(credentials.seed,"lambda-cloud-audit-1",{version:1,audit:audit.id,
        inventory:audit.inventory,checked:checked,bad:0,heads:audit.heads,base:audit.base})^
    return save(handle,config,{*:updated,attestation:report})^
}
pub pn prepare(handle, credentials, config) any^ {
    let task = status(handle,config)^
    if (task == null or task.attestation == null) raise error("sync audit: verified report required")
    return maintenance.prepare(handle,credentials,config,{id:task.audit,attestation:task.attestation})^
}
pub pn abort(handle, credentials, config) any^ {
    let task = status(handle,config)^
    if (task == null) raise error("sync audit: no local audit")
    let pending = maintenance.status(handle,config)^
    if (pending != null) { maintenance.abort(handle,credentials,config)^ }
    let remote = transport.call(config,credentials,"audit_status",{})^
    if (remote != null and remote.id == task.audit) {
        let canceled = transport.call(config,credentials,"audit_abort",{id:task.audit})^
        if (not canceled.aborted) raise error("sync audit: abort contention; retry")
    }
    save(handle,config,null)^
    return {aborted:true}
}
