import local: lambda.sync.local
import crypto: lambda.io.crypto
pn main() {
    let area = "temp/sync-validation-cache-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/tree")^
    let secret = crypto.key()^
    let raw = local.create(area ++ "/repo.sqlite",secret,"validation-cache",area ++ "/tree","device")^
    output("one",area ++ "/tree/file")^
    let cached = {*:raw,keys:{*:raw.keys,cache_enabled:true}}
    let before = local.scan(cached)^
    let handle = {*:cached,keys:{*:cached.keys,cached_head:before.repository_cache}}
    let bad_time = local.mutate(handle,pn (tx,k,current)=> {
        return {*:current,endpoint:{*:current.endpoint,replica:{*:current.endpoint.replica,
            time:float(current.endpoint.replica.time)^}}}
    }) ^ { ^ }
    let base = before.state.base
    let reordered = {*:base,checkpoint:{*:base.checkpoint,
        replica:{time:base.checkpoint.replica.time,*:base.checkpoint.replica}}}
    // Native equality ignores map order; the unchanged saved digest must still fail.
    let bad_base = local.mutate(handle,pn (tx,k,current)=> { return {*:current,base:reordered} }) ^ { ^ }
    let held = local.status(handle)^
    output("two",area ++ "/tree/file")^
    let scanned = local.scan(handle,["file"])^
    let reopened = local.open_repository(raw.path,secret,"validation-cache")^
    let result = [bad_time is error,base == reordered,bad_base is error,
        held.generation == before.generation,scanned.generation > before.generation,
        scanned.state.review == null,scanned.repository_cache.validation.base_projection != null,
        local.status(reopened)^.generation == scanned.generation,input(area ++ "/tree/file",'text')^ == "two"]
    io.delete(area)^
    return all(result)
}
