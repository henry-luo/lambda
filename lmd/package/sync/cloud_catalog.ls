// one conditional catalog serializes publication and maintenance; no lease can bypass its epoch.
import codec: .codec
import schema: .cloud_schema
import v: .schema

pub fn data_key(kind, digest, epoch=0) any^ => kind ++ "/" ++
    (if (epoch == 0) "" else string(epoch) ++ "/") ++ digest
pub fn epoch(key) any^ {
    let parts = split(key,"/")^
    return if (len(parts)^ == 2) 0 else if (len(parts)^ == 3) int(parts[1])^ else -1
}
pub pn load(store, prefix, control, initialize=false) any^ {
    var found = store.get(prefix ++ "catalog")^
    if (found == null) {
        if (not initialize and (control.storage == "catalog-1" or store.get(prefix ++ "catalog-required")^ != null))
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
        let initial = {version:1,epoch:0,phase:'open',heads:heads,objects:[],
            checkpoint:null,maintenance:null,pins:[],uploads:[],legacy:len(heads)^ > 0,backup:null}
        checkpoint_write(store,prefix,{etag:null},initial)^
        found = store.get(prefix ++ "catalog")^
    }
    let decoded = codec.decode(found.body)^
    v.record(decoded,["version","epoch","phase","heads","objects","checkpoint","maintenance","pins"],["uploads","legacy","backup"])^
    let value = {*:decoded,uploads:if (decoded.uploads == null) [] else decoded.uploads,
        legacy:decoded.legacy or false,backup:decoded.backup}
    if (value.version != 1 or not v.natural(value.epoch)^ or not contains(['open','maintenance'],value.phase)^)
        raise error("sync cloud: invalid publication catalog")
    immutable(store,prefix ++ "catalog-required",{version:1})^
    return {value:value,etag:found.etag}
}
pub pn write(store, prefix, before, value) any^ {
    return store.write(prefix ++ "catalog",codec.encode(value)^,before.etag)^
}
pub fn head(catalog, actor) any^ => v.lookup([for (entry in catalog.heads) {*:entry,id:entry.actor}],actor)^
pub fn writable(catalog, key, owner=false) any^ {
    let expected = catalog.epoch+(if (catalog.phase == 'maintenance') 1 else 0)
    if ((catalog.phase == 'maintenance' and not owner) or epoch(key)^ != expected)
        raise error("sync cloud: publication paused or storage epoch retired")
    return true
}
pub pn reserve(store, prefix, control, keys, owner=false, actor=null) any^ {
    var attempts = 0
    while (attempts < 16) {
        let current = load(store,prefix,control)^
        for (key in keys) { writable(current.value,key,owner)^ }
        let objects = sort(unique([*current.value.objects,*keys]))^
        let previous = v.lookup(current.value.uploads,actor)^
        let uploads = if (actor == null) current.value.uploads else [
            *[for (entry in current.value.uploads where entry.id != actor) entry],
            {id:actor,keys:sort(unique([*keys,*if (previous == null) [] else previous.keys]))^}]
        let next = {*:current.value,objects:objects,uploads:sort(uploads,(entry)=>entry.id)^}
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
