// one conditional catalog serializes publication and maintenance; no lease can bypass its epoch.
import codec: .codec
import schema: .cloud_schema
import v: .schema
import pages: .metadata_pages

pub fn data_key(kind, digest, epoch=0) any^ => kind ++ "/" ++
    (if (epoch == 0) "" else string(epoch) ++ "/") ++ digest
pub fn epoch(key) any^ => schema.storage_epoch(key)^
pub fn normalize(decoded) any^ {
    v.record(decoded,["version","epoch","phase","heads","objects","checkpoint","maintenance","pins"],
        ["uploads","legacy","backup","objects_root","quarantine_root","retirements_root","audit","next_epoch"])^
    let value = {*:decoded,uploads:if (decoded.uploads == null) [] else decoded.uploads,
        legacy:decoded.legacy or false,backup:decoded.backup}
    if (not contains([1,2],value.version)^ or not v.natural(value.epoch)^ or not contains(['open','maintenance','audit'],value.phase)^)
        raise error("sync cloud: invalid publication catalog")
    if (value.objects_root != null) { pages.valid_ref(value.objects_root)^ }
    if (value.version == 2 and (not v.natural(next_epoch(value)^)^ or next_epoch(value)^ <= value.epoch))
        raise error("sync cloud: invalid next storage generation")
    return value
}
pub pn load(store, prefix, control, initialize=false) any^ {
    var found = store.get(prefix ++ "catalog")^
    if (found == null) {
        if (not initialize and (contains(["catalog-1","catalog-2"],control.storage)^ or store.get(prefix ++ "catalog-required")^ != null))
            raise error("sync cloud: publication catalog missing; explicit backup recovery required")
        var heads = []
        // upgrade requires the previous handler to be stopped; new clients use the same endpoint.
        for (entry in control.members) {
            let old = store.get(prefix ++ "heads/" ++ entry.device.id)^
            if (old != null) {
                let publication = codec.decode(old.body)^
                heads = [*heads,{actor:entry.device.id,publication:publication,etag:old.etag,at:0}]
            }
        }
        let initial = {version:if (control.storage == "catalog-2") 2 else 1,epoch:0,phase:'open',heads:heads,objects:[],
            objects_root:null,quarantine_root:null,retirements_root:null,next_epoch:if (control.storage == "catalog-2") 1 else null,
            checkpoint:null,maintenance:null,pins:[],uploads:[],legacy:len(heads)^ > 0,backup:null}
        checkpoint_write(store,prefix,{etag:null},initial)^
        found = store.get(prefix ++ "catalog")^
    }
    let value = normalize(codec.decode(found.body)^)^
    immutable(store,prefix ++ "catalog-required",{version:1})^
    return {value:value,etag:found.etag}
}
pub pn write(store, prefix, before, value) any^ {
    return store.write(prefix ++ "catalog",codec.encode(value)^,before.etag)^
}
pub fn head(catalog, actor) any^ => v.lookup([for (entry in catalog.heads) {*:entry,id:entry.actor}],actor)^
pub fn writable(catalog, key, owner=false) any^ {
    let expected = target_epoch(catalog)^
    if (catalog.phase == 'audit' or (catalog.phase != 'open' and not owner) or epoch(key)^ != expected)
        raise error("sync cloud: publication paused or storage epoch retired")
    return true
}
pub fn next_epoch(value) any^ => if (value.version == 1 or value.next_epoch == null) value.epoch+1 else value.next_epoch
pub fn target_epoch(value) any^ => if (value.phase == 'open') value.epoch else next_epoch(value)^
pub pn available_epoch(store, prefix, value) any^ {
    let found = store.get(prefix ++ "generation")^
    return max(next_epoch(value)^,if (found == null) 1 else codec.decode(found.body)^.next)
}
pub pn claim_epoch(store, prefix, value, requested, id) any^ {
    // a separate high-water mark survives catalog loss; immutable claims prevent reuse after its recovery.
    let found = store.get(prefix ++ "generation")^
    let available = max(next_epoch(value)^,if (found == null) 1 else codec.decode(found.body)^.next)
    if (requested != available) { return false }
    let changed = store.write(prefix ++ "generation",codec.encode({version:1,next:requested+1})^,
        if (found == null) null else found.etag)^
    if (not changed.written) { return false }
    let claimed = store.write(prefix ++ "allocations/" ++ string(requested),codec.encode({version:1,epoch:requested,id:id})^,null)^
    return claimed.written
}
pub fn upload_count(entry) any^ => if (entry.root == null) len(entry.keys)^ else entry.root.count
pub pn registered(store, prefix, value, key) any^ {
    return if (value.version == 1) contains(value.objects,key)^ else
        pages.lookup(store,prefix,value.objects_root,key)^ != null
}
pub pn referenceable(store, prefix, value, key, owner=false) any^ {
    if (value.version == 1 or value.phase == 'audit' or
        (value.phase == 'maintenance' and value.maintenance.audit == true)) { return writable(value,key,owner)^ }
    let physical_epoch = epoch(key)^
    if ((value.phase != 'open' and not owner) or
        (physical_epoch > value.epoch and (value.phase == 'open' or physical_epoch != target_epoch(value)^)) or
        not registered(store,prefix,value,key)^)
        raise error("sync cloud: retired or unregistered physical key")
    return true
}
pub pn reserve(store, prefix, control, keys, owner=false, actor=null) any^ {
    var attempts = 0
    while (attempts < 16) {
        let current = load(store,prefix,control)^
        for (key in keys) { writable(current.value,key,owner)^ }
        let objects = if (current.value.version == 1) sort(unique([*current.value.objects,*keys]))^ else []
        let objects_root = if (current.value.version == 1) null else pages.upsert_many(store,prefix,
            current.value.objects_root,[for (key in keys) {id:key,value:null}])^
        let previous = v.lookup(current.value.uploads,actor)^
        let uploads = if (actor == null) current.value.uploads else [
            *[for (entry in current.value.uploads where entry.id != actor) entry],
            if (current.value.version == 1) {id:actor,keys:sort(unique([*keys,*if (previous == null) [] else previous.keys]))^}
            else {id:actor,root:pages.upsert_many(store,prefix,if (previous == null) null else previous.root,
                [for (key in keys) {id:key,value:null}])^}]
        let next = {*:current.value,objects:objects,objects_root:objects_root,uploads:sort(uploads,(entry)=>entry.id)^}
        if (next == current.value) { return current }
        if (len(objects)^ > 32768) raise error("sync cloud: catalog object budget exceeded")
        let applied = write(store,prefix,current,next)^
        if (applied.written) { return {value:next,etag:applied.etag} }
        attempts = attempts+1
    }
    raise error("sync cloud: catalog contention; retry")
}

pub pn immutable(store, key, value) any^ {
    let result = store.write(key,codec.encode(value)^,null)^
    if (not result.written) {
        let previous = store.get(key)^
        if (previous == null or codec.decode(previous.body)^ != value) raise error("sync cloud: immutable record collision")
    }
    return true
}
pub pn checkpoint_write(store, prefix, before, value) any^ {
    let backup = {*:value,backup:null}
    let id = schema.digest(backup)^
    immutable(store,prefix ++ "catalogs/" ++ id,backup)^
    let result = write(store,prefix,before,{*:value,backup:id})^
    return {*:result,backup:id}
}
