// immutable committed jobs fence each retired physical path across all subsequent catalog epochs.
import catalog: .cloud_catalog
import receipts: .cloud_receipts
import pages: .metadata_pages
import schema: .cloud_schema
import codec: .codec
import core: .core
import v: .schema

pn committed(store, prefix, value, control, id) any^ {
    if (value.maintenance != null and value.maintenance.id == id and value.maintenance.committed and
        value.maintenance.aborted != true and value.maintenance.receipt != null) {
        schema.maintenance_checkpoint(value.maintenance.receipt.checkpoint,control)^
        catalog.immutable(store,prefix ++ "retirements/" ++ id,value.maintenance.receipt)^
    }
    let found = store.get(prefix ++ "retirements/" ++ id)^
    if (found == null) raise error("sync cloud: retirement job was not committed")
    let job = codec.decode(found.body,262144)^
    v.fields(job,["version","id","plan","epoch","from","keep","checkpoint"])^
    let cut = schema.maintenance_checkpoint(job.checkpoint,control)^
    if (job.version != 1 or job.id != id or schema.digest(job.plan)^ != id or cut.epoch != job.epoch)
        raise error("sync cloud: invalid committed retirement job")
    return job
}
pn progress(store, prefix, id) any^ {
    let found = store.get(prefix ++ "retirement-progress/" ++ id)^
    return if (found == null) {value:{next:0,cursor:null,complete:false},etag:null}
        else {value:codec.decode(found.body,262144)^,etag:found.etag}
}
pub pn summary(store, prefix, job) any^ {
    if (job == null) { return null }
    let current = if (job.committed and job.aborted != true) progress(store,prefix,job.id)^.value
        else {next:0,complete:false}
    return {id:job.id,committed:job.committed,aborted:job.aborted == true,next:current.next,total:job.total}
}
pub pn handle(store, prefix, control, current, envelope, milliseconds) any^ {
    let value = current.value
    let kind = envelope.kind
    let body = envelope.body
    if (kind == "maint_prune" or kind == "maint_reap") {
        v.fields(body,["id","budget"])^
        if (value.legacy or value.phase != 'open' or not schema.id(body.id)^ or
            not v.natural(body.budget)^ or body.budget < 1 or body.budget > 64)
            raise error("sync cloud: deletion requires audited open state and a bounded job")
        let job = committed(store,prefix,value,control,body.id)^
        let before = progress(store,prefix,body.id)^
        let initial = if (kind == "maint_reap" and before.value.complete) {next:0,cursor:null,complete:false}
            else before.value
        if (initial.complete) { return {advanced:true,next:initial.next,total:initial.next,complete:true,removed:0} }
        let part = pages.page(store,prefix,job.from,initial.cursor,body.budget)^
        var removed = 0
        for (entry in part.entries) {
            let kept = [for (root in job.keep where root != null) pages.lookup(store,prefix,root,entry.id)^ != null]
            if (not any(kept)) {
                if (not schema.data_key(entry.id)^ or catalog.epoch(entry.id)^ >= job.epoch)
                    raise error("sync cloud: refusing a non-retired physical path")
                store.erase(prefix ++ entry.id)^
                removed = removed+1
            }
        }
        let next = {next:initial.next+len(part.entries)^,cursor:part.next,complete:part.next == null}
        let written = store.write(prefix ++ "retirement-progress/" ++ body.id,codec.encode(next)^,before.etag)^
        return {advanced:written.written,next:if (written.written) next.next else before.value.next,
            total:if (job.from == null) 0 else job.from.count,complete:written.written and next.complete,removed:removed}
    }
    if (envelope.actor != control.owner.id) raise error("sync cloud: owner maintenance authority required")
    if (kind == "maint_begin") {
        v.record(body,["expected","plan"],["audit","attestation"])^
        v.fields(body.plan,["version","epoch","heads","fingerprint"])^
        let id = schema.digest(body.plan)^
        let old = value.maintenance
        if (old != null and old.id == id) {
            if (old.aborted == true) raise error("sync cloud: aborted maintenance cannot resume")
            return {started:true,id:id,committed:old.committed,epoch:body.plan.epoch}
        }
        let auditing = body.audit != null
        if (auditing) {
            let report = schema.verify(body.attestation,"lambda-cloud-audit-1",control.owner.public_key)^
            v.fields(report,["version","audit","inventory","checked","bad","heads","base"])^
            if (value.phase != 'audit' or value.audit == null or value.audit.id != body.audit or
                value.audit.stage != 'complete' or report.version != 1 or report.audit != body.audit or
                report.inventory != value.audit.inventory or report.bad != 0 or
                report.checked != (if (report.inventory == null) 0 else report.inventory.count) or
                report.heads != value.audit.heads or report.base != value.audit.base)
                raise error("sync audit: incomplete or stale owner verification")
        }
        if ((value.legacy and not auditing) or value.phase != (if (auditing) 'audit' else 'open') or current.etag != body.expected or
            len(value.uploads)^ != 0 or body.plan.version != 1 or
            body.plan.heads != schema.catalog_heads(value)^)
            raise error("sync cloud: stale, unaudited or active publication state")
        if (old != null and old.aborted != true and not progress(store,prefix,old.id)^.value.complete)
            raise error("sync cloud: finish the previous retirement job")
        if (not catalog.claim_epoch(store,prefix,value,body.plan.epoch,id)^)
            { return {started:false,id:id,committed:false,replan:true,epoch:body.plan.epoch} }
        let job = {id:id,plan:body.plan,from:value.objects_root,total:if (value.objects_root == null) 0 else value.objects_root.count,
            committed:false,aborted:false,receipt:null,audit:auditing}
        let changed = catalog.write(store,prefix,current,{*:value,phase:'maintenance',maintenance:job,next_epoch:body.plan.epoch})^
        return {started:changed.written,id:id,committed:false,epoch:body.plan.epoch}
    }
    let job = value.maintenance
    if (job == null or job.id != body.id) raise error("sync cloud: unknown maintenance job")
    if (kind == "maint_abort") {
        v.fields(body,["id"])^
        if (job.committed) raise error("sync cloud: cannot abort a committed cut")
        let changed = catalog.write(store,prefix,current,{*:value,phase:'open',next_epoch:job.plan.epoch+1,
            uploads:[for (entry in value.uploads where entry.id != envelope.actor) entry],
            maintenance:{*:job,committed:true,aborted:true},audit:if (job.audit == true) null else value.audit})^
        return {aborted:changed.written}
    }
    if (kind != "maint_commit") raise error("sync cloud: unsupported paged maintenance request")
    v.fields(body,["id","checkpoint"])^
    if (job.committed) {
        if (value.checkpoint != body.checkpoint) raise error("sync cloud: changed maintenance checkpoint")
        committed(store,prefix,value,control,job.id)^
        return {committed:true,epoch:value.epoch}
    }
    let selected = schema.maintenance_checkpoint(body.checkpoint,control)^
    if (selected.epoch != job.plan.epoch or selected.covered != job.plan.heads)
        raise error("sync cloud: incomplete maintenance coverage")
    let published = schema.publication(selected.publication,control)^
    for (head in value.heads) {
        if (not core.dominates(published.clock,head.publication.body.clock)^)
            raise error("sync cloud: maintenance loses a published frontier")
    }
    let root = receipts.publication(store,prefix,published,selected.epoch)^
    for (pin in value.pins) {
        let keys = if (job.audit == true) [for (id in pin.objects) catalog.data_key("objects",id,selected.epoch)^]
            else if (pin.keys == null) [for (id in pin.objects) catalog.data_key("objects",id,value.epoch)^] else pin.keys
        for (key in keys) {
            if (pages.lookup(store,prefix,root,key)^ == null) raise error("sync cloud: maintenance loses a reserved cache copy")
        }
    }
    let quarantine = if (job.audit == true) value.audit.quarantine else value.quarantine_root
    let record = {version:1,id:job.id,plan:job.plan,epoch:selected.epoch,from:job.from,
        keep:[root,*if (quarantine == null) [] else [quarantine]],checkpoint:body.checkpoint}
    let history = pages.upsert(store,prefix,value.retirements_root,job.id,{epoch:selected.epoch})^
    let changed = catalog.checkpoint_write(store,prefix,current,{*:value,phase:'open',epoch:selected.epoch,next_epoch:selected.epoch+1,
        checkpoint:body.checkpoint,objects:[],objects_root:root,retirements_root:history,
        quarantine_root:quarantine,legacy:false,audit:null,
        pins:if (job.audit != true) value.pins else [for (pin in value.pins)
            {*:pin,keys:[for (id in pin.objects) catalog.data_key("objects",id,selected.epoch)^]}],
        uploads:[for (entry in value.uploads where entry.id != envelope.actor) entry],
        maintenance:{*:job,committed:true,receipt:record}})^
    if (changed.written) { catalog.immutable(store,prefix ++ "retirements/" ++ job.id,record)^ }
    return {committed:changed.written,epoch:if (changed.written) selected.epoch else value.epoch}
}
