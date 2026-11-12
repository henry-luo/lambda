// One bounded invocation; every authoritative state transition is conditional object storage.
import schema: .cloud_schema
import codec: .codec
import v: .schema
import core: .core

pn immutable(store, key, value) any^ {
    let bytes = codec.encode(value)^
    let result = store.write(key,bytes,null)^
    if (not result.written) {
        let old = store.get(key)^
        if (old == null or codec.decode(old.body)^ != value) raise error("sync cloud: immutable record collision")
    }
    return true
}
pub pn bootstrap(store, signed_control) any^ {
    let control = schema.control(signed_control,signed_control.body.owner.id)^
    if (control.generation != 1) raise error("sync cloud: initial generation must be one")
    let result = store.write(schema.prefix(control.space)^ ++ "control",codec.encode(signed_control)^,null)^
    if (not result.written) raise error("sync cloud: space already exists")
    return true
}
pn head(store, prefix, actor) any^ {
    let found = store.get(prefix ++ "heads/" ++ actor)^
    return if (found == null) null else {publication:codec.decode(found.body)^,etag:found.etag}
}
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
    if (envelope.kind == "pull") {
        v.fields(body,[])^
        var heads = []
        for (entry in control.members) {
            let value = head(store,prefix,entry.device.id)^
            if (value != null) {
                schema.publication(value.publication,control)^
                heads = [*heads,{actor:entry.device.id,*:value}]
            }
        }
        // every pull starts at the roster again; a concurrent publication is seen next round.
        return {control:signed_control,control_etag:record.etag,heads:heads}
    }
    if (envelope.kind == "read") {
        v.fields(body,["keys"])^
        batch(body.keys)^
        var result = []
        for (key in body.keys) {
            if (not schema.readable(key)^) raise error("sync cloud: read scope rejected")
            result = [*result,{key:key,capability:store.capability("GET",prefix ++ key,{})^}]
        }
        return result
    }
    if (envelope.kind == "upload") {
        v.fields(body,["objects"])^
        batch(body.objects)^
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
        let receipt = {version:1,keys:ordered}
        let id = schema.digest(receipt)^
        let existing = store.get(prefix ++ "receipts/" ++ id)^
        if (existing == null) {
            for (key in ordered) {
                if (store.head(prefix ++ key)^ == null) raise error("sync cloud: missing dependency")
            }
            immutable(store,prefix ++ "receipts/" ++ id,receipt)^
        }
        else if (codec.decode(existing.body)^ != receipt) raise error("sync cloud: receipt collision")
        return {id:id}
    }
    if (envelope.kind == "commit") {
        v.fields(body,["publication","expected"])^
        let next = schema.publication(body.publication,control)^
        if (next.actor != envelope.actor) raise error("sync cloud: cannot replace another device's head")
        let digest = schema.digest(body.publication)^
        let current = head(store,prefix,envelope.actor)^
        if (current != null and schema.digest(current.publication)^ == digest) { return {*:current,published:true} }
        let previous = if (current == null) null else schema.publication(current.publication,control)^
        let expected_previous = if (current == null) null else schema.digest(current.publication)^
        if (next.sequence != (if (previous == null) 1 else previous.sequence+1) or next.previous != expected_previous or
            (previous != null and not core.dominates(next.clock,previous.clock)^) or
            body.expected != (if (current == null) null else current.etag))
            { return {published:false,head:current} }
        var dependencies = []
        for (id in next.receipts) {
            let found = store.get(prefix ++ "receipts/" ++ id)^
            if (found == null) raise error("sync cloud: missing receipt")
            let receipt = codec.decode(found.body,32768)^
            if (schema.digest(receipt)^ != id) raise error("sync cloud: invalid receipt")
            dependencies = [*dependencies,*receipt.keys]
        }
        if (schema.dependency_digest(dependencies)^ != next.dependencies or
            not contains(dependencies,next.bundle.key)^) raise error("sync cloud: incomplete publication")
        immutable(store,prefix ++ "publications/" ++ digest,body.publication)^
        // membership is re-read immediately before admission; no immediate revocation fence is claimed.
        let latest = store.get(prefix ++ "control")^
        if (latest.etag != record.etag) raise error("sync cloud: membership changed; reload before publication")
        let applied = store.write(prefix ++ "heads/" ++ envelope.actor,codec.encode(body.publication)^,body.expected) ^ { ^ }
        let committed = if (applied is error) false else applied.written
        if (not committed^) {
            let observed = head(store,prefix,envelope.actor)^
            if (observed != null and schema.digest(observed.publication)^ == digest) { return {*:observed,published:true} }
            if (applied is error) raise applied
            return {published:false,head:observed}
        }
        return {published:true,publication:body.publication,etag:applied^.etag}
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
    raise error("sync cloud: unsupported request")
}
