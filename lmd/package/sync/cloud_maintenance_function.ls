// the owner chooses encrypted reachability; the function fences epochs and executes only that persisted plan.
import catalog: .cloud_catalog
import schema: .cloud_schema
import codec: .codec
import core: .core
import v: .schema

pub fn heads(value) any^ => sort([for (entry in value.heads)
    {actor:entry.actor,sequence:entry.publication.body.sequence,publication:schema.digest(entry.publication)^}],(entry)=>entry.actor)^
pub fn checkpoint(value, control) any^ {
    let body = schema.verify(value,"lambda-cloud-maintenance-1",control.owner.public_key)^
    v.fields(body,["version","epoch","covered","publication"] )^
    if (body.version != 1 or not v.natural(body.epoch)^ or body.epoch < 1 or not (body.covered is array))
        raise error("sync cloud: invalid maintenance checkpoint")
    let publication = schema.publication(body.publication,control)^
    if (publication.actor != control.owner.id or catalog.epoch(publication.bundle.key)^ != body.epoch)
        raise error("sync cloud: wrong maintenance authority/epoch")
    return body
}
pn dependencies(store, prefix, publication) any^ {
    var keys = []
    for (id in publication.receipts) {
        let found = store.get(prefix ++ "receipts/" ++ id)^
        if (found == null) raise error("sync cloud: missing maintenance receipt")
        let value = codec.decode(found.body)^
        if (schema.digest(value)^ != id) raise error("sync cloud: invalid maintenance receipt")
        keys = [*keys,*value.keys]
    }
    let ordered = sort(unique(keys))^
    if (schema.dependency_digest(ordered)^ != publication.dependencies or not contains(ordered,publication.bundle.key)^)
        raise error("sync cloud: incomplete maintenance publication")
    for (key in ordered) {
        if (catalog.epoch(key)^ != catalog.epoch(publication.bundle.key)^ or store.head(prefix ++ key)^ == null)
            raise error("sync cloud: wrong epoch or missing maintenance dependency")
    }
    return ordered
}
pub pn handle(store, prefix, control, envelope, milliseconds) any^ {
    let kind = envelope.kind
    let body = envelope.body
    if (kind == "recover_catalog") {
        v.fields(body,["snapshot"])^
        if (envelope.actor != control.owner.id or not schema.id(body.snapshot)^ or store.get(prefix ++ "catalog")^ != null)
            raise error("sync cloud: owner-selected recovery requires a missing catalog")
        let stored = store.get(prefix ++ "catalogs/" ++ body.snapshot)^
        if (stored == null) raise error("sync cloud: catalog backup missing")
        let candidate = codec.decode(stored.body)^
        if (schema.digest(candidate)^ != body.snapshot or candidate.version != 1 or candidate.phase != 'open')
            raise error("sync cloud: invalid catalog backup")
        let cut = if (candidate.checkpoint == null) null else checkpoint(candidate.checkpoint,control)^
        if (cut != null) {
            if (cut.epoch != candidate.epoch) raise error("sync cloud: backup epoch mismatch")
            dependencies(store,prefix,schema.publication(cut.publication,control)^)^
        } else if (candidate.epoch != 0) { raise error("sync cloud: backup checkpoint missing") }
        for (head in candidate.heads) {
            let publication = schema.publication(head.publication,control)^
            let covered = if (cut == null) [] else [for (entry in cut.covered where entry.actor == head.actor) entry]
            if (len(covered)^ == 0 or publication.sequence > covered[0].sequence) {
                if (catalog.epoch(publication.bundle.key)^ != candidate.epoch) raise error("sync cloud: backup head references retired storage")
                dependencies(store,prefix,publication)^
            }
        }
        let recovered = {*:candidate,backup:body.snapshot,legacy:true}
        let result = store.write(prefix ++ "catalog",codec.encode(recovered)^,null)^
        return {recovered:result.written,maintenance_allowed:false,epoch:candidate.epoch}
    }
    let current = catalog.load(store,prefix,control)^
    let value = current.value
    if (kind == "cancel_publication") {
        v.fields(body,[])^
        if (value.phase != 'open') raise error("sync cloud: finish/abort maintenance before cancelling publication")
        let changed = catalog.write(store,prefix,current,{*:value,
            uploads:[for (entry in value.uploads where entry.id != envelope.actor) entry]})^
        return {cancelled:changed.written}
    }
    if (kind == "cache_pin") {
        v.fields(body,["id","objects"])^
        if (value.phase != 'open' or not schema.id(body.id)^ or not (body.objects is array) or
            len(body.objects)^ < 1 or len(body.objects)^ > 128 or not all([for (id in body.objects) schema.id(id)^]))
            raise error("sync cloud: invalid cache reservation")
        let old = v.lookup(value.pins,body.id)^
        let pin = {id:body.id,actor:envelope.actor,objects:sort(unique(body.objects))^}
        if (old != null) {
            if (old != pin) raise error("sync cloud: reservation ID reused")
            return {reserved:true,id:body.id,epoch:value.epoch}
        }
        for (id in pin.objects) {
            if (store.head(prefix ++ catalog.data_key("objects",id,value.epoch)^)^ == null)
                raise error("sync cloud: cache copy unavailable")
        }
        let changed = catalog.write(store,prefix,current,{*:value,pins:[*value.pins,pin]})^
        return {reserved:changed.written,id:body.id,epoch:value.epoch}
    }
    if (kind == "cache_release") {
        v.fields(body,["id"])^
        let old = v.lookup(value.pins,body.id)^
        if (old == null) { return {released:true} }
        if (old.actor != envelope.actor and envelope.actor != control.owner.id)
            raise error("sync cloud: reservation owner required")
        let changed = catalog.write(store,prefix,current,{*:value,pins:[for (pin in value.pins where pin.id != body.id) pin]})^
        return {released:changed.written}
    }
    if (kind == "maint_prune" or kind == "maint_reap") {
        if (value.legacy) raise error("sync cloud: audited inventory required after catalog recovery/upgrade")
        let confirmed = checkpoint(value.checkpoint,control)^
        if (confirmed.epoch != value.epoch) raise error("sync cloud: deletion checkpoint epoch mismatch")
        v.fields(body,["id","budget"])^
        let plan = value.maintenance
        if (plan == null or plan.id != body.id or not plan.committed or plan.aborted == true or not v.natural(body.budget)^ or
            body.budget < 1 or body.budget > 64) raise error("sync cloud: invalid deletion step")
        let start = if (kind == "maint_reap" and plan.next == len(plan.old_keys)^) 0 else plan.next
        let end = min(len(plan.old_keys)^,start+body.budget)
        for (offset in range(start,end,1)) {
            let key = plan.old_keys[offset]
            if (not schema.data_key(key)^ or catalog.epoch(key)^ < 0 or catalog.epoch(key)^ >= value.epoch) raise error("sync cloud: refusing current storage epoch deletion")
            store.erase(prefix ++ key)^
        }
        let changed = catalog.write(store,prefix,current,{*:value,maintenance:{*:plan,next:end}})^
        return {advanced:changed.written,next:if (changed.written) end else plan.next,
            total:len(plan.old_keys)^,complete:changed.written and end == len(plan.old_keys)^}
    }
    if (envelope.actor != control.owner.id) raise error("sync cloud: owner maintenance authority required")
    if (kind == "maint_begin") {
        v.fields(body,["expected","plan"])^
        v.fields(body.plan,["version","epoch","heads","fingerprint"])^
        let id = schema.digest(body.plan)^
        if (value.maintenance != null and value.maintenance.id == id and value.maintenance.aborted == true)
            raise error("sync cloud: aborted maintenance cannot resume")
        if (value.maintenance != null and value.maintenance.id == id)
            { return {started:true,id:id,committed:value.maintenance.committed,epoch:body.plan.epoch} }
        if (value.legacy) raise error("sync cloud: legacy store needs an audited inventory before destructive maintenance")
        if (len(value.uploads)^ != 0) raise error("sync cloud: pending publications protect uploaded objects; resume or cancel their writer")
        if (current.etag != body.expected or value.phase != 'open' or body.plan.version != 1 or
            body.plan.epoch != value.epoch+1 or body.plan.heads != heads(value)^ or
            (value.maintenance != null and value.maintenance.next != len(value.maintenance.old_keys)^))
            raise error("sync cloud: stale or unfinished maintenance plan")
        let previous_keys = if (value.maintenance == null) [] else value.maintenance.old_keys
        let old_keys = sort(unique([*previous_keys,*[for (key in value.objects where catalog.epoch(key)^ <= value.epoch) key]]))^
        if (len(old_keys)^ > 32768) raise error("sync cloud: retired key budget exceeded")
        let plan = {id:id,plan:body.plan,old_keys:old_keys,next:0,committed:false}
        let changed = catalog.write(store,prefix,current,{*:value,phase:'maintenance',maintenance:plan})^
        return {started:changed.written,id:id,committed:false,epoch:body.plan.epoch}
    }
    if (kind == "maint_abort") {
        v.fields(body,["id"])^
        let plan = value.maintenance
        if (plan == null or plan.id != body.id or plan.committed)
            raise error("sync cloud: only an uncommitted plan can be aborted")
        let changed = catalog.write(store,prefix,current,{*:value,phase:'open',uploads:[for (entry in value.uploads where entry.id != envelope.actor) entry],maintenance:{*:plan,
            committed:true,aborted:true,next:len(plan.old_keys)^}})^
        return {aborted:changed.written}
    }
    if (kind == "maint_commit") {
        v.fields(body,["id","checkpoint"])^
        let plan = value.maintenance
        if (plan == null or plan.id != body.id) raise error("sync cloud: unknown maintenance plan")
        if (plan.committed) {
            if (value.checkpoint != body.checkpoint) raise error("sync cloud: maintenance checkpoint changed")
            return {committed:true,epoch:value.epoch}
        }
        let selected = checkpoint(body.checkpoint,control)^
        if (selected.epoch != value.epoch+1 or selected.covered != plan.plan.heads)
            raise error("sync cloud: checkpoint does not cover the frozen catalog")
        let publication = schema.publication(selected.publication,control)^
        for (entry in value.heads) {
            if (not core.dominates(publication.clock,entry.publication.body.clock)^)
                raise error("sync cloud: maintenance loses a published frontier")
        }
        let keys = dependencies(store,prefix,publication)^
        for (pin in value.pins) {
            for (id in pin.objects) {
                if (not contains(keys,catalog.data_key("objects",id,selected.epoch)^)^)
                    raise error("sync cloud: maintenance loses a reserved cache copy")
            }
        }
        let changed = catalog.checkpoint_write(store,prefix,current,{*:value,epoch:selected.epoch,phase:'open',
            objects:[for (key in value.objects where catalog.epoch(key)^ == selected.epoch) key],
            checkpoint:body.checkpoint,uploads:[for (entry in value.uploads where entry.id != envelope.actor) entry],maintenance:{*:plan,committed:true}})^
        return {committed:changed.written,epoch:if (changed.written) selected.epoch else value.epoch}
    }
    raise error("sync cloud: unsupported request")
}
