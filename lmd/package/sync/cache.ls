// only cold chunks can leave a local archive; authenticated remote reservations keep their other copy alive.
import local: .local
import repo: .repository
import reach: .reachability
import documents: .documents
import cloud: .cloud
import transport: .cloud_transport
import bundle: .cloud_bundle
import schema: .cloud_schema
import codec: .codec
import v: .schema
import hex: lambda.security.tuf.metadata
import sqlite: lambda.io.sqlite
import metadata: .cache_state

fn pin_request(id, ids, locations, protocol) any^ => {id:id,objects:[for (object_id in ids) hex.hex(object_id)^],
    *:if (protocol != 2) {} else {keys:[for (entry in locations) entry.key]}}
pub fn physical_key(pin, id, epoch, fallback=null) any^ {
    let keys = if (pin == null or pin.keys == null) [] else
        [for (key in pin.keys where ends_with(key,"/" ++ hex.hex(id)^)^) key]
    return if (len(keys)^ > 0) keys[0] else if (fallback != null) fallback else bundle.object_key(id,epoch)^
}
pub pn plan(handle, credentials, config, limit=64) any^ {
    if (not v.natural(limit)^ or limit < 1 or limit > 128) raise error("sync cache: invalid eviction budget")
    let selected = repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let current = documents.expand(tx,k,repo.load(tx,k)^.state)^
        if (current.cache_job != null) raise error("sync cache: resume the existing eviction")
        let protected = reach.hot(tx,k,current)^
        let retained = reach.roots(tx,k,current,false)^
        return take([for (row in sqlite.query(tx,"SELECT id FROM objects WHERE kind IN ('chunk', 'chunk_raw') ORDER BY id")^
            where contains(retained,row[0])^ and not contains(protected,row[0])^) row[0]],limit)
    })^
    if (len(selected)^ == 0) { return {id:null,objects:[],complete:true} }
    let id = schema.digest(["cache",uuid()^,selected])^
    let job = {version:1,id:id,provider:cloud.provider(config)^,objects:selected,next:0,
        owner:credentials.device.id,verified:false,cancelled:false}
    local.mutate(handle,pn (tx,k,current)=> {
        if (current.cache_job != null) raise error("sync cache: another eviction was admitted")
        return {*:current,cache_job:job}
    })^
    let remote = transport.call(config,credentials,"pull",{})^
    let control = schema.control(remote.control,config.authority)^
    if (control.space != credentials.space) raise error("sync cache: wrong provider space")
    let locations = [for (entry in local.cloud_locations(handle,cloud.provider(config)^,selected)^)
        {id:entry.id,key:if (entry.key == null) bundle.object_key(entry.id,remote.epoch or 0)^ else entry.key}]
    let pin = transport.call(config,credentials,"cache_pin",pin_request(id,selected,locations,remote.protocol)^)^
    if (not pin.reserved) raise error("sync cache: remote reservation contention; retry")
    // authenticate actual remote bytes, rather than treating an existence check as a verified copy.
    for (object_id in selected) {
        let record = codec.decode(transport.read(config,credentials,physical_key({keys:[for (entry in locations where entry.id == object_id) entry.key]},object_id,pin.epoch)^)^)^
        if (record.id != object_id or not repo.is_chunk(record.kind)^) raise error("sync cache: wrong remote copy")
        repo.validate_record(handle.keys,record)^
    }
    local.mutate(handle,pn (tx,k,current)=> {
        if (current.cache_job.id != id or current.cache_job.cancelled) raise error("sync cache: eviction intent changed")
        return {*:current,cache_job:{*:job,verified:true}}
    })^
    return job
}
pub pn step(handle, config, id, budget=16) any^ {
    if (not v.natural(budget)^ or budget < 1 or budget > 128) raise error("sync cache: invalid eviction budget")
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let loaded = repo.load(tx,k)^
        let current = documents.expand(tx,k,loaded.state)^
        let job = current.cache_job
        if (job == null or job.id != id or job.provider != cloud.provider(config)^ or not job.verified or job.cancelled)
            raise error("sync cache: unknown eviction job")
        let protected = reach.hot(tx,k,current)^
        let end = min(len(job.objects)^,job.next+budget)
        var evicted = if (current.cache_evicted == null) [] else current.cache_evicted
        for (offset in range(job.next,end,1)) {
            let target = job.objects[offset]
            if (contains(protected,target)^) raise error("sync cache: chunk became protected; hydrate/cancel or replan")
            if (repo.present(k,target)^ and not repo.is_chunk(repo.record(k,target)^.kind)^)
                raise error("sync cache: metadata eviction is forbidden")
            evicted = [*[for (entry in evicted where entry.id != target) entry],{id:target,pin:id,provider:job.provider,owner:job.owner}]
        }
        let next = metadata.flush(tx,k,{*:loaded.state,cache_job:if (end == len(job.objects)^) null else {*:job,next:end},
            cache_evicted:evicted,previous:loaded.id})^
        // publishing this intent inside the transaction fences other writers before external effects.
        repo.commit_head(tx,k,loaded.generation,next)^
        for (offset in range(job.next,end,1)) { repo.remove_object(tx,k,job.objects[offset])^ }
        return {id:id,next:end,total:len(job.objects)^,complete:end == len(job.objects)^}
    })^
}
pub pn hydrate(handle, credentials, config, budget=64) any^ {
    if (not v.natural(budget)^ or budget < 1 or budget > 128) raise error("sync cache: invalid hydration budget")
    let current = local.status(handle)^.state
    if (current.cache_job != null and not current.cache_job.cancelled)
        raise error("sync cache: finish or cancel the pending eviction before hydration")
    let provider = cloud.provider(config)^
    let entries = if (current.cache_evicted == null) [] else current.cache_evicted
    let selected = take([for (entry in entries where entry.provider == provider) entry],budget)
    let remote = transport.call(config,credentials,"pull",{})^
    let control = schema.control(remote.control,config.authority)^
    if (control.space != credentials.space) raise error("sync cache: wrong provider space")
    let epoch = remote.epoch or 0
    let known = if (remote.protocol == 2) local.cloud_locations(handle,provider,[for (entry in selected) entry.id])^ else []
    let unresolved = [for (entry in selected where v.lookup(known,entry.id)^.key == null and
        v.lookup(remote.pins,entry.pin)^.keys == null) entry.id]
    let discovered = if (remote.protocol == 2 and len(unresolved)^ > 0)
        cloud.resolve_locations(credentials,config,remote,unresolved)^ else []
    var restored = []
    for (entry in selected) {
        let pin = v.lookup(remote.pins,entry.pin)^
        let key = if (v.lookup(known,entry.id)^.key != null) v.lookup(known,entry.id)^.key else v.lookup(discovered,entry.id)^.key
        let record = codec.decode(transport.read(config,credentials,physical_key(pin,entry.id,epoch,key)^)^)^
        if (record.id != entry.id or not repo.is_chunk(record.kind)^)
            raise error("sync cache: durable reservation missing; local metadata retained for recovery")
        repo.validate_record(handle.keys,record)^
        local.import_objects(handle,[record])^
        restored = [*restored,entry.id]
    }
    local.mutate(handle,pn (tx,k,current)=> {
        for (id in restored) { repo.record(k,id)^ }
        return metadata.flush(tx,k,{*:current,cache_evicted:[for (entry in (if (current.cache_evicted == null) [] else current.cache_evicted)
            where not contains(restored,entry.id)^) entry]})^
    })^
    local.release_staging(handle,restored)^
    let remaining = local.status(handle)^.state.cache_evicted
    for (id in unique([for (entry in selected where entry.owner == credentials.device.id) entry.pin]) where not any([for (entry in remaining) entry.pin == id])) {
        transport.call(config,credentials,"cache_release",{id:id})^
    }
    return {restored:len(restored)^,remaining:len([for (entry in remaining where entry.provider == provider) entry])}
}

pub pn cancel(handle, credentials, config) any^ {
    let job = local.status(handle)^.state.cache_job
    if (job == null) raise error("sync cache: no eviction intent")
    local.mutate(handle,pn (tx,k,current)=> { return {*:current,cache_job:{*:job,cancelled:true}} })^
    var result = hydrate(handle,credentials,config,128)^
    while (result.remaining > 0) { result = hydrate(handle,credentials,config,128)^ }
    let released = transport.call(config,credentials,"cache_release",{id:job.id})^
    if (not released.released) raise error("sync cache: reservation release contention; retry cancellation")
    local.mutate(handle,pn (tx,k,current)=> { return {*:current,cache_job:null} })^
    return {cancelled:true,id:job.id}
}
