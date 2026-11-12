// shared full-space attestations describe deferred history chunks; active data still requires local bytes.
import repo: .repository
import v: .schema
import schema: .cloud_schema

pub fn entries(current) => if (current.cache_evicted == null) [] else current.cache_evicted
pub fn ids(current) => unique([*[for (entry in entries(current)) entry.id],
    *if (current.cache_job == null or not current.cache_job.verified) [] else current.cache_job.objects])
pub pn flush(tx, keys, current) any^ {
    let items = sort(entries(current),(entry)=>entry.id)^
    let id = if (len(items)^ == 0) null else repo.store_object(tx,keys,"cache",
        {version:1,space:keys.space,entries:items})^
    return {*:current,cache_root:id}
}
pub pn install(tx, keys, current, id) any^ {
    if (id == null) { return current }
    let value = repo.get(tx,keys,id,"cache")^
    v.fields(value,["version","space","entries"])^
    if (value.version != 1 or value.space != keys.space or not (value.entries is array) or len(value.entries)^ > 32768)
        raise error("sync cache: invalid availability manifest")
    var selected = entries(current)
    for (entry in value.entries) {
        v.fields(entry,["id","pin","provider","owner"])^
        if (not (entry.id is binary) or len(entry.id)^ != 32 or not schema.id(entry.pin)^ or
            not schema.id(entry.provider)^ or not schema.id(entry.owner)^)
            raise error("sync cache: invalid deferred chunk")
        if (not repo.present(keys,entry.id)^ and v.lookup(selected,entry.id)^ == null) { selected = [*selected,entry] }
    }
    return flush(tx,keys,{*:current,cache_evicted:selected})^
}

// filesystem removal can survive a rolled-back SQLite scope; the verified intent reconstructs its declaration.
pub pn recover(tx, keys, current) any^ {
    let job = current.cache_job
    if (job == null or not job.verified) { return current }
    var updated = entries(current)
    for (id in job.objects where not repo.present(keys,id)^ and v.lookup(updated,id)^ == null) {
        updated = [*updated,{id:id,pin:job.id,provider:job.provider,owner:job.owner}]
    }
    return flush(tx,keys,{*:current,cache_evicted:updated})^
}
