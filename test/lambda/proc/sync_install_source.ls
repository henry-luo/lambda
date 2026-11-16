import local: lambda.sync.local
import repo: lambda.sync.repository
import disk: lambda.io.disk
import crypto: lambda.io.crypto
import hex: lambda.security.tuf.metadata
import h: ~~.mod_sync_local_helpers
pn main() {
    let area = "temp/sync-install-source-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/a")^; io.mkdir(area ++ "/b")^
    let secret = crypto.key()^
    let a = local.create(area ++ "/a.sqlite",secret,"install-source",area ++ "/a","a")^
    let b = local.create(area ++ "/b.sqlite",secret,"install-source",area ++ "/b","b")^
    output("first",area ++ "/a/file")^
    local.scan(a)^; h.transfer(a,b)^
    let before = local.status(b)^
    let prepared = local.prepare_install(b)^
    let journal = prepared.state.installation
    let path = hex.hex(journal.source.state)^ ++ ".object"
    let physical = disk.capture(b.keys.objects,path,8388608)^
    disk.erase(b.keys.objects,path,physical.token)^
    let reopened = local.open_repository(b.path,secret,"install-source")^
    let rejected = local.materialize(reopened) ^ { ^ }
    let held = local.status(reopened)^
    let guarded = rejected is error and held.generation == prepared.generation and
        held.state.installation.next == 0 and disk.inspect(area ++ "/b","file")^ == null
    disk.install(b.keys.objects,path,physical.bytes,null)^
    let completed = local.materialize(reopened)^
    let first = input(area ++ "/b/file",'text')^
    output("second",area ++ "/a/file")^; local.scan(a)^; h.transfer(a,reopened)^
    local.prepare_install(reopened)^
    // A prior build's inline intent remains readable after restart.
    local.mutate(reopened,pn (tx,k,current)=> {
        let old = repo.get(tx,k,current.installation.source.state,"state")^
        return {*:current,installation:{*:current.installation,source:null,version:null,
            endpoint:{replica:old.endpoint.replica}}}
    })^
    let legacy = local.materialize(reopened)^
    let result = [journal.version == 2,journal.source.state == before.id,journal.endpoint == null,
        guarded,completed.state.installation == null,first == "first",
        legacy.state.installation == null,input(area ++ "/b/file",'text')^ == "second"]
    io.delete(area)^
    return all(result)
}
