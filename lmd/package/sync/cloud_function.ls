// One bounded invocation; every authoritative state transition is conditional object storage.
import schema: .cloud_schema
import codec: .codec
import v: .schema
import core: .core
import catalog: .cloud_catalog
import maintenance: .cloud_maintenance_function
import receipts: .cloud_receipts
import retirements: .cloud_retirements
import audit: .cloud_audit_function

pub pn bootstrap(store, signed_control) any^ {
    let control = schema.control(signed_control,signed_control.body.owner.id)^
    if (control.generation != 1) raise error("sync cloud: initial generation must be one")
    let prefix = schema.prefix(control.space)^
    if (store.get(prefix ++ "control")^ != null) raise error("sync cloud: space already exists")
    catalog.load(store,prefix,control,true)^
    let result = store.write(prefix ++ "control",codec.encode(signed_control)^,null)^
    if (not result.written) raise error("sync cloud: space already exists")
    return true
}
pn head(store, prefix, control, actor) any^ {
    let found = catalog.head(catalog.load(store,prefix,control)^.value,actor)^
    return if (found == null) null else {publication:found.publication,etag:found.etag,at:found.at}
}
fn read_headers(etags, index) => if (etags == null) {} else {'if-match':etags[index]}
fn batch(items) any^ {
    if (not (items is array) or len(items) < 1 or len(items) > 64) raise error("sync cloud: batch limit")
    return true
}
pub pn handle(store, request, milliseconds) any^ {
    let space = request.body.space
    if (not v.actor(space)^) raise error("sync cloud: invalid space")
    let prefix = schema.prefix(space)^
    let record = store.get(prefix ++ "control")^
    if (record == null) raise error("sync cloud: unknown space")
    let signed_control = codec.decode(record.body,131072)^
    let control = schema.control(signed_control,signed_control.body.owner.id)^
    let envelope = schema.authenticate(request,control,milliseconds)^
    let body = envelope.body
    if (contains(["backup_discover","audit_begin","audit_step","audit_status","audit_abort"],envelope.kind)^) {
        return audit.handle(store,prefix,control,envelope)^
    }
    if (envelope.kind == "pull") {
        v.fields(body,[])^
        let captured = catalog.load(store,prefix,control)^
        var heads = []
        for (entry in control.members) {
            let value = catalog.head(captured.value,entry.device.id)^
            if (value != null) {
                schema.publication(value.publication,control)^
                heads = [*heads,{actor:entry.device.id,*:value}]
            }
        }
        // every pull starts at the roster again; a concurrent publication is seen next round.
        return {control:signed_control,control_etag:record.etag,heads:heads,
            epoch:captured.value.epoch,catalog_etag:captured.etag,phase:captured.value.phase,
            catalog_backup:captured.value.backup,maintenance_allowed:not captured.value.legacy,
            protocol:captured.value.version,
            next_epoch:catalog.available_epoch(store,prefix,captured.value)^,
            audit:audit.summary(captured.value)^,
            checkpoint:captured.value.checkpoint,
            maintenance:if (captured.value.version == 2) retirements.summary(store,prefix,captured.value.maintenance)^
                else if (captured.value.maintenance == null) null else {
                id:captured.value.maintenance.id,committed:captured.value.maintenance.committed,aborted:captured.value.maintenance.aborted == true,
                next:captured.value.maintenance.next,total:len(captured.value.maintenance.old_keys)^},
            pins:captured.value.pins,pending_publications:[for (entry in captured.value.uploads)
                {actor:entry.id,objects:catalog.upload_count(entry)^}]}
    }
    if (envelope.kind == "read") {
        v.record(body,["keys"],["etags"])^
        batch(body.keys)^
        if (body.etags != null and (not (body.etags is array) or len(body.etags)^ != len(body.keys)^ or
            not all([for (tag in body.etags) tag is string and len(tag)^ > 0 and len(tag)^ <= 256 and
                not contains(tag,"\r")^ and not contains(tag,"\n")^])))
            raise error("sync cloud: invalid observed ETags")
        var result = []
        for (index in 0 to (len(body.keys)^-1)) {
            let key = body.keys[index]
            if (not schema.readable(key)^) raise error("sync cloud: read scope rejected")
            result = [*result,{key:key,capability:store.capability("GET",prefix ++ key,read_headers(body.etags,index)^)^}]
        }
        return result
    }
    if (envelope.kind == "upload") {
        v.fields(body,["objects"])^
        batch(body.objects)^
        for (item in body.objects) { schema.descriptor(item)^ }
        catalog.reserve(store,prefix,control,[for (item in body.objects) item.key],envelope.actor == control.owner.id,envelope.actor)^
        var result = []
        for (item in body.objects) {
            schema.descriptor(item)^
            let existing = store.head(prefix ++ item.key)^
            result = [*result,{key:item.key,exists:existing != null,
                capability:if (existing != null) store.capability("GET",prefix ++ item.key,{})^ else store.capability("PUT",prefix ++ item.key,
                    {'if-none-match':"*",'x-amz-checksum-sha256':item.checksum,'content-length':string(item.bytes)})^}]
        }
        return result
    }
    if (envelope.kind == "stage") {
        v.fields(body,["keys"])^
        batch(body.keys)^
        let ordered = sort(unique(body.keys))
        for (key in ordered) {
            if (not schema.data_key(key)^) raise error("sync cloud: dependency scope rejected")
        }
        let captured = catalog.load(store,prefix,control)^
        if (captured.value.version == 2) {
            for (key in ordered) {
                catalog.referenceable(store,prefix,captured.value,key,envelope.actor == control.owner.id)^
                if (store.head(prefix ++ key)^ == null) raise error("sync cloud: missing dependency")
            }
            return receipts.stage(store,prefix,ordered,catalog.target_epoch(captured.value)^)^
        }
        catalog.reserve(store,prefix,control,ordered,envelope.actor == control.owner.id,envelope.actor)^
        let receipt = {version:1,keys:ordered}
        let id = schema.digest(receipt)^
        let existing = store.get(prefix ++ "receipts/" ++ id)^
        if (existing == null) {
            for (key in ordered) {
                if (store.head(prefix ++ key)^ == null) raise error("sync cloud: missing dependency")
            }
            catalog.immutable(store,prefix ++ "receipts/" ++ id,receipt)^
        }
        else if (codec.decode(existing.body)^ != receipt) raise error("sync cloud: receipt collision")
        return {id:id}
    }
    if (envelope.kind == "stage_merge") {
        v.fields(body,["receipts"])^
        let captured = catalog.load(store,prefix,control)^
        if (captured.value.version != 2 or (captured.value.phase != 'open' and envelope.actor != control.owner.id))
            raise error("sync cloud: paged publication unavailable")
        return receipts.merge(store,prefix,body.receipts,catalog.target_epoch(captured.value)^)^
    }
    if (envelope.kind == "commit") {
        v.fields(body,["publication","expected"])^
        let next = schema.publication(body.publication,control)^
        if (next.actor != envelope.actor) raise error("sync cloud: cannot replace another device's head")
        let digest = schema.digest(body.publication)^
        let captured = catalog.load(store,prefix,control)^
        if (captured.value.phase != 'open') raise error("sync cloud: publication paused for maintenance")
        let current = catalog.head(captured.value,envelope.actor)^
        if (current != null and schema.digest(current.publication)^ == digest) { return {*:current,published:true} }
        let previous = if (current == null) null else schema.publication(current.publication,control)^
        let expected_previous = if (current == null) null else schema.digest(current.publication)^
        if (next.sequence != (if (previous == null) 1 else previous.sequence+1) or next.previous != expected_previous or
            (previous != null and not core.dominates(next.clock,previous.clock)^) or
            body.expected != (if (current == null) null else current.etag))
            { return {published:false,head:current} }
        var dependencies = []
        if (next.version == 2) {
            if (captured.value.version != 2) raise error("sync cloud: upgrade the publication catalog first")
            receipts.publication(store,prefix,next,captured.value.epoch)^
        } else {
        for (id in next.receipts) {
            let found = store.get(prefix ++ "receipts/" ++ id)^
            if (found == null) raise error("sync cloud: missing receipt")
            let receipt = codec.decode(found.body,32768)^
            if (schema.digest(receipt)^ != id) raise error("sync cloud: invalid receipt")
            dependencies = [*dependencies,*receipt.keys]
        }
        if (schema.dependency_digest(dependencies)^ != next.dependencies or
            not contains(dependencies,next.bundle.key)^) raise error("sync cloud: incomplete publication")
        for (key in dependencies) { catalog.writable(captured.value,key)^ }
        }
        catalog.immutable(store,prefix ++ "publications/" ++ digest,body.publication)^
        // membership is re-read immediately before admission; no immediate revocation fence is claimed.
        let latest = store.get(prefix ++ "control")^
        if (latest.etag != record.etag) raise error("sync cloud: membership changed; reload before publication")
        let next_catalog = {*:captured.value,uploads:[for (entry in captured.value.uploads where entry.id != envelope.actor) entry],heads:[*[for (entry in captured.value.heads where entry.actor != envelope.actor) entry],
            {actor:envelope.actor,publication:body.publication,etag:digest,at:milliseconds}]}
        let applied = catalog.checkpoint_write(store,prefix,captured,next_catalog) ^ { ^ }
        let committed = if (applied is error) false else applied.written
        if (not committed^) {
            let observed = head(store,prefix,control,envelope.actor)^
            if (observed != null and schema.digest(observed.publication)^ == digest) { return {*:observed,published:true} }
            if (applied is error) raise applied
            return {published:false,head:observed}
        }
        return {published:true,publication:body.publication,etag:digest}
    }
    if (envelope.kind == "membership") {
        v.fields(body,["control","expected"])^
        if (envelope.actor != control.owner.id) raise error("sync cloud: owner authority required")
        let next = schema.control(body.control,control.owner.id)^
        if (next.space != control.space or next.generation != control.generation+1 or body.expected != record.etag or
            not all([for (old in control.members) any([for (entry in next.members) entry.device == old.device])]))
            raise error("sync cloud: stale control or historical identity removed")
        return store.write(prefix ++ "control",codec.encode(body.control)^,body.expected)^
    }
    return maintenance.handle(store,prefix,control,envelope,milliseconds)^

}

// private scheduler entry: deployment authority selects the space; public requests still authenticate above.
pub pn maintain(store, space, budget=32, reap=false) any^ {
    let prefix = schema.prefix(space)^
    let control_record = store.get(prefix ++ "control")^
    if (control_record == null) raise error("sync cloud: unknown scheduled space")
    let signed = codec.decode(control_record.body)^
    let control = schema.control(signed,signed.body.owner.id)^
    let current = catalog.load(store,prefix,control)^
    if (current.value.legacy or current.value.maintenance == null or not current.value.maintenance.committed or current.value.maintenance.aborted == true)
        { return {idle:true,reason:"a key holder must prepare and commit maintenance"} }
    maintenance.checkpoint(current.value.checkpoint,control)^
    return maintenance.handle(store,prefix,control,{actor:control.owner.id,
        kind:if (reap) "maint_reap" else "maint_prune",body:{id:current.value.maintenance.id,budget:budget}},now().unix)^
}
