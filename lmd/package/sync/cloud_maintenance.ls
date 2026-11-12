// key holders prepare encrypted replacement checkpoints; finite function calls copy, cut and reap.
import local: .local
import cache: .cache
import cloud: .cloud
import repo: .repository
import bundle: .cloud_bundle
import transport: .cloud_transport
import schema: .cloud_schema
import handler: .cloud_maintenance_function
import catalog: .cloud_catalog
import reach: .reachability
import retention: .maintenance_state
import codec: .codec
import v: .schema
import hex: lambda.security.tuf.metadata
import inventory_pages: .cloud_inventory

fn jobs(current) => if (current.cloud_jobs == null) [] else current.cloud_jobs
pn save_job(handle, provider, job) any^ {
    return local.mutate(handle,pn (tx,k,current)=> {
        let previous = v.lookup(jobs(current),provider)^
        let id = repo.store_object(tx,k,"maintenance",job)^
        return {*:current,cloud_jobs:[*[for (entry in jobs(current) where entry.id != provider) entry],
            {id:provider,object:id}],staged_objects:unique([
                *[for (physical_id in reach.staged(current) where previous == null or physical_id != previous.object) physical_id],id])}
    })^
}
pub pn status(handle, config) any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let selected = v.lookup(jobs(repo.load(tx,k)^.state),cloud.provider(config)^)^
        return if (selected == null) null else repo.get(tx,k,selected.object,"maintenance")^
    })^
}
pub pn prepare(handle, credentials, config, audit=null) any^ {
    if (credentials.device.id != config.authority) raise error("sync cloud: owner maintenance key required")
    if (status(handle,config)^ != null) raise error("sync cloud: resume or abort the existing maintenance job")
    if (local.status(handle)^.state.cache_job != null) raise error("sync cloud: finish/cancel the pending cache job first")
    let observed = transport.call(config,credentials,"pull",{})^
    let control = schema.control(observed.control,config.authority)^
    if (control.space != credentials.space) raise error("sync cloud: wrong control space")
    if (observed.protocol != 2 or audit != null) {
        var hydrated = cache.hydrate(handle,credentials,config,128)^
        while (hydrated.remaining > 0) { hydrated = cache.hydrate(handle,credentials,config,128)^ }
    }
    let remote = cloud.pull(handle,credentials,config)^
    if (remote.maintenance_allowed == false and audit == null) raise error("sync cloud: audited inventory required after catalog recovery/upgrade")
    if (remote.phase != (if (audit == null) 'open' else 'audit')) raise error("sync cloud: existing maintenance must finish or abort")
    if (len(remote.pending_publications)^ > 0) raise error("sync cloud: a writer must resume/cancel its pending publication")
    let epoch = remote.next_epoch or ((remote.epoch or 0)+1)
    // outstanding cache reservations are explicit shared roots, including otherwise retired endpoints.
    let have = local.inventory(handle)^
    let pinned = unique([for (pin in remote.pins) for (id in pin.objects) hex.unhex(id)^])
    for (id in pinned where not contains(have,id)^) {
        let record = codec.decode(transport.read(config,credentials,pin_key(remote,id)^)^)^
        if (record.id != id) raise error("sync cloud: cache reservation object mismatch")
        local.import_objects(handle,[record])^
    }
    let selected = repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let current = repo.load(tx,k)^.state
        return sort(unique([*reach.roots(tx,k,current,false)^,
            *reach.closure(tx,k,pinned,retention.get(current)^)^]))^
    })^
    if (remote.protocol == 2) { return prepare_paged(handle,credentials,config,remote,selected,audit)^ }
    let captured = bundle.capture(handle,credentials,epoch,selected)^
    let current = local.status(handle)^.state
    let plan = {version:1,epoch:epoch,heads:handler.heads({heads:remote.heads})^,
        fingerprint:codec.digest([captured.payload,captured.key,retention.get(current)^.generation,current.snapshots])^}
    let job = {version:1,id:schema.digest(plan)^,plan:plan,expected:remote.catalog_etag,
        payload:captured.payload,key:captured.key,bytes:captured.bytes,next:0,
        retention_generation:retention.get(current)^.generation,snapshots:current.snapshots}
    save_job(handle,cloud.provider(config)^,job)^
    return {id:job.id,objects:len(selected)^,epoch:epoch,next:0}
}
pn clear_job(handle, config) any^ {
    return local.mutate(handle,pn (tx,k,current)=> {
        let provider = cloud.provider(config)^
        let previous = v.lookup(jobs(current),provider)^
        return {*:current,cloud_jobs:[for (entry in jobs(current) where entry.id != provider) entry],
            staged_objects:[for (id in reach.staged(current) where previous == null or id != previous.object) id]}
    })^
}
pub pn step(handle, credentials, config, budget=32) any^ {
    if (not v.natural(budget)^ or budget < 1 or budget > 128) raise error("sync cloud: invalid maintenance budget")
    let job = status(handle,config)^
    if (job == null) raise error("sync cloud: no maintenance job")
    let current = local.status(handle)^.state
    if (retention.get(current)^.generation != job.retention_generation or current.snapshots != job.snapshots) {
        abort(handle,credentials,config)^
        return {id:job.id,replan:true,committed:false,next:0,total:len(job.payload.ids)^}
    }
    let remote = transport.call(config,credentials,"pull",{})^
    schema.control(remote.control,config.authority)^
    if ((remote.maintenance == null or remote.maintenance.id != job.id) and (remote.catalog_etag != job.expected or (remote.next_epoch != null and remote.next_epoch != job.plan.epoch))) {
        clear_job(handle,config)^
        return {id:job.id,replan:true,committed:false,next:0,total:len(job.payload.ids)^}
    }
    if (job.protocol == 2) { return step_paged(handle,credentials,config,job,budget)^ }
    let started = transport.call(config,credentials,"maint_begin",{expected:job.expected,plan:job.plan})^
    if (not started.started) raise error("sync cloud: catalog changed before maintenance; replan")
    let end = min(len(job.payload.ids)^,job.next+budget)
    if (not started.committed) {
        for (offset in range(job.next,end,1)) {
            let id = job.payload.ids[offset]
            let encoded = codec.encode(local.export_objects(handle,[id])^[0])^
            let key = bundle.object_key(id,job.plan.epoch)^
            let copied = transport.ensure(config,credentials,transport.descriptor(key,encoded)^,encoded)^
            if (copied.existing != null) {
                if (not copied.existing.ok) raise error("sync cloud: replacement copy unavailable")
                let existing = codec.decode(copied.existing.body)^
                if (existing.id != id) raise error("sync cloud: replacement copy mismatch")
                local.import_objects(handle,[existing])^
            }
        }
        if (end < len(job.payload.ids)^) {
            save_job(handle,cloud.provider(config)^,{*:job,next:end})^
            return {id:job.id,next:end,total:len(job.payload.ids)^,committed:false}
        }
        let stored = transport.ensure(config,credentials,transport.descriptor(job.key,job.bytes)^,job.bytes)^
        if (stored.existing != null and (not stored.existing.ok or
            hash(stored.existing.body,'sha256')^ != hash(job.bytes,'sha256')^))
            raise error("sync cloud: replacement bundle mismatch")
        let dependencies = bundle.dependencies(job.payload.ids,job.key)^
        var receipts = []
        for (offset in range(0,len(dependencies)^,64)) {
            let receipt = transport.call(config,credentials,"stage",{keys:slice(dependencies,offset,offset+64)^})^
            receipts = [*receipts,receipt.id]
        }
        let publication = schema.signed(credentials.seed,"lambda-cloud-publication-1",{version:1,space:credentials.space,
            actor:credentials.device.id,sequence:1,previous:null,clock:job.payload.saved.frontier,
            bundle:transport.descriptor(job.key,job.bytes)^,inventory:schema.digest(job.payload.ids)^,
            dependencies:schema.dependency_digest(dependencies)^,receipts:receipts})^
        let checkpoint = schema.signed(credentials.seed,"lambda-cloud-maintenance-1",
            {version:1,epoch:job.plan.epoch,covered:job.plan.heads,publication:publication})^
        let committed = transport.call(config,credentials,"maint_commit",{id:job.id,checkpoint:checkpoint})^
        if (not committed.committed) raise error("sync cloud: maintenance cut contention; retry")
    }
    clear_job(handle,config)^
    cloud.pull(handle,credentials,config)^
    local.release_staging(handle,job.payload.ids)^
    return {id:job.id,next:len(job.payload.ids)^,total:len(job.payload.ids)^,committed:true,epoch:job.plan.epoch}
}
pub fn pin_keys(remote) any^ => unique([for (pin in remote.pins) for (key in
    (if (pin.keys == null) [for (id in pin.objects) catalog.data_key("objects",id,remote.epoch or 0)^] else pin.keys)) key])
pub fn pin_key(remote, id) any^ {
    let found = [for (key in pin_keys(remote)^ where ends_with(key,"/" ++ hex.hex(id)^)^) key]
    if (len(found)^ == 0) raise error("sync cloud: pinned physical copy unavailable")
    return found[0]
}
pn prepare_paged(handle, credentials, config, remote, selected, audit) any^ {
    let epoch = remote.next_epoch or ((remote.epoch or 0)+1)
    let previous = local.cloud_locations(handle,cloud.provider(config)^,selected)^
    let locations = [for (entry in previous) {id:entry.id,key:if (audit != null or entry.key == null)
        bundle.object_key(entry.id,epoch)^ else entry.key}]
    let extra = if (audit != null) [] else [for (key in pin_keys(remote)^ where
        not any([for (entry in locations) entry.key == key])) key]
    let captured = bundle.capture(handle,credentials,epoch,selected,{root:null,locations:locations})^
    let current = local.status(handle)^.state
    let plan = {version:1,epoch:epoch,heads:handler.heads({heads:remote.heads})^,
        fingerprint:codec.digest([captured.payload,uuid()^,retention.get(current)^.generation,current.snapshots])^}
    let job = {version:1,protocol:2,id:schema.digest(plan)^,plan:plan,expected:remote.catalog_etag,
        payload:captured.payload,key:null,bytes:null,next:0,extra_keys:extra,audit:audit,copied:0,
        retention_generation:retention.get(current)^.generation,snapshots:current.snapshots}
    save_job(handle,cloud.provider(config)^,job)^
    return {id:job.id,objects:len(selected)^,epoch:epoch,next:0,
        copy_objects:len([for (entry in locations where catalog.epoch(entry.key)^ == epoch) entry])^}
}
fn begin_request(job) => {expected:job.expected,plan:job.plan,
    *:if (job.audit == null) {} else {audit:job.audit.id,attestation:job.audit.attestation}}
pn step_paged(handle, credentials, config, job, budget) any^ {
    let started = transport.call(config,credentials,"maint_begin",begin_request(job)^)^
    if (started.replan == true) {
        clear_job(handle,config)^
        return {id:job.id,replan:true,committed:false,next:0,total:len(job.payload.ids)^}
    }
    if (not started.started) raise error("sync cloud: changed catalog; replan maintenance")
    var task = job
    if (not started.committed) {
        let end = min(len(task.payload.ids)^,task.next+budget)
        var copied = task.copied
        for (entry in slice(task.payload.locations,task.next,end)^) {
            if (catalog.epoch(entry.key)^ == task.plan.epoch) {
                let encoded = codec.encode(local.export_objects(handle,[entry.id])^[0])^
                let stored = transport.ensure(config,credentials,transport.descriptor(entry.key,encoded)^,encoded)^
                if (stored.existing != null) {
                    let existing = if (stored.existing.ok) codec.decode(stored.existing.body)^ else null
                    if (existing == null or existing.id != entry.id) raise error("sync cloud: replacement copy unavailable")
                    local.import_objects(handle,[existing])^
                }
                if (stored.uploaded) { copied = copied+1 }
            }
        }
        if (end < len(task.payload.ids)^) {
            save_job(handle,cloud.provider(config)^,{*:task,next:end,copied:copied})^
            return {id:task.id,next:end,total:len(task.payload.ids)^,committed:false,copied:copied}
        }
        let objects = inventory_pages.stage(config,credentials,[*[for (entry in task.payload.locations) entry.key],*task.extra_keys],task.plan.epoch)^
        if (task.bytes == null) {
            let sealed = bundle.reseal(credentials,task.payload,if (objects == null) null else objects.root,task.plan.epoch)^
            task = {*:task,*:sealed,next:end,copied:copied}
            // persist the exact bundle before a network effect so a lost cut response cannot change it.
            save_job(handle,cloud.provider(config)^,task)^
        }
        let stored = transport.ensure(config,credentials,transport.descriptor(task.key,task.bytes)^,task.bytes)^
        if (stored.existing != null and (not stored.existing.ok or hash(stored.existing.body,'sha256')^ != hash(task.bytes,'sha256')^))
            raise error("sync cloud: replacement bundle mismatch")
        let bundle_receipt = inventory_pages.stage(config,credentials,[task.key],task.plan.epoch)^
        let combined = inventory_pages.merge(config,credentials,[bundle_receipt,objects],task.plan.epoch)^
        let publication = schema.signed(credentials.seed,"lambda-cloud-publication-1",{version:2,space:credentials.space,
            actor:credentials.device.id,sequence:1,previous:null,clock:task.payload.saved.frontier,
            bundle:transport.descriptor(task.key,task.bytes)^,inventory:schema.digest(task.payload.inventory)^,
            dependencies:combined.id,receipts:[combined.id]})^
        let checkpoint = schema.signed(credentials.seed,"lambda-cloud-maintenance-1",
            {version:1,epoch:task.plan.epoch,covered:task.plan.heads,publication:publication})^
        let result = transport.call(config,credentials,"maint_commit",{id:task.id,checkpoint:checkpoint})^
        if (not result.committed) raise error("sync cloud: maintenance cut contention; retry")
    }
    clear_job(handle,config)^
    cloud.pull(handle,credentials,config)^
    local.release_staging(handle,task.payload.ids)^
    return {id:task.id,next:len(task.payload.ids)^,total:len(task.payload.ids)^,committed:true,epoch:task.plan.epoch,copied:task.copied}
}
pub pn abort(handle, credentials, config) any^ {
    let job = status(handle,config)^
    if (job == null) raise error("sync cloud: no maintenance job")
    let remote = transport.call(config,credentials,"pull",{})^
    if (remote.maintenance != null and remote.maintenance.id == job.id and not remote.maintenance.committed) {
        let result = transport.call(config,credentials,"maint_abort",{id:job.id})^
        if (not result.aborted) raise error("sync cloud: maintenance abort contention; retry")
    }
    clear_job(handle,config)^
    return {aborted:true,id:job.id}
}
pub pn prune(credentials, config, id, budget=32, reap=false) any^ {
    return transport.call(config,credentials,if (reap) "maint_reap" else "maint_prune",{id:id,budget:budget})^
}
