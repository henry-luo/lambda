import repo: lambda.sync.repository
import local: lambda.sync.local
import packs: lambda.sync.pack_store
import pack: lambda.sync.pack
import sqlite: lambda.io.sqlite
import crypto: lambda.io.crypto
import disk: lambda.io.disk
pn main() {
    let area = "temp/sync-packs-" ++ string(sys.proc.self.pid)
    io.mkdir(area ++ "/tree")^
    let handle = local.create(area ++ "/repo",crypto.key()^,"pack-test",area ++ "/tree","device")^
    let saved = repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let a = repo.store_object(tx,k,"chunk_raw",binary("first")^)^
        let b = repo.store_object(tx,k,"chunk_raw",binary("second")^)^
        return [a,b]
    })^
    let before = disk.entries(handle.path ++ ".objects","",1000)^
    let checked = repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let a = packs.location(k,saved[0])^
        let b = packs.location(k,saved[1])^
        let header = repo.load(tx,k)^
        let duplicate = repo.store_object(tx,k,"chunk_raw",binary("first")^)^
        let rebuilt = repo.rebuild_index(tx,k)^
        return [a.pack == b.pack,duplicate == saved[0],repo.get(tx,k,saved[0])^ == binary("first")^,
            rebuilt.rebuilt,repo.load(tx,k)^.id == header.id,repo.verify(tx,k)^.verified]
    })^
    let interrupted = repo.access(handle.path,handle.keys,pn (tx,k)=> {
        repo.store_object(tx,k,"chunk_raw",binary("orphan")^)^
        packs.flush(tx,k)^
        raise error("test transaction interrupted after durable pack")
    }) ^ { ^ }
    // A crash before scratch unlink can leave an empty reserved name behind.
    let scratch = ".lambda-spool-0123456789abcdef0123456789abcdef"
    let nonempty = ".lambda-spool-abcdef0123456789abcdef0123456789"
    let unrelated = ".lambda-spool-not-a-spool"
    let invalid = ".lambda-spool-gggggggggggggggggggggggggggggggg"
    output("",handle.path ++ ".objects/" ++ scratch)^
    output("keep",handle.path ++ ".objects/" ++ nonempty)^
    output("",handle.path ++ ".objects/" ++ unrelated)^
    output("",handle.path ++ ".objects/" ++ invalid)^
    repo.records({*:handle.keys,objects:handle.path ++ ".objects"})^
    let unlocked_kept = disk.inspect(handle.path ++ ".objects",scratch)^ != null
    let recovered = repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let found = repo.recover(tx,k)^
        repo.rebuild_index(tx,k)^
        return len(found)^ == 1 and repo.get(tx,k,found[0].id)^ == binary("orphan")^
    })^
    let cleaned_scratch = disk.inspect(handle.path ++ ".objects",scratch)^ == null
    let preserved = input(handle.path ++ ".objects/" ++ nonempty,'text')^ == "keep" and
        disk.inspect(handle.path ++ ".objects",unrelated)^ != null and
        disk.inspect(handle.path ++ ".objects",invalid)^ != null
    repo.access(handle.path,handle.keys,pn (tx,k)=> { return repo.remove_object(tx,k,saved[0])^ })^
    let retained = repo.access(handle.path,handle.keys,pn (tx,k)=> {
        return not repo.present(k,saved[0])^ and repo.get(tx,k,saved[1])^ == binary("second")^ and repo.verify(tx,k)^.verified
    })^
    let filename = [for (entry in disk.entries(handle.path ++ ".objects","",1000)^ where ends_with(entry.name,".pack")^) entry.name][0]
    let original = disk.capture(handle.path ++ ".objects",filename,pack.maximum)^
    let corrupted = slice(original.bytes,0,len(original.bytes)^-1)^ ++ binary([255-int(original.bytes[len(original.bytes)^-1])^])^
    disk.install(handle.path ++ ".objects",filename,corrupted,original.token)^
    let rejected = local.verify(handle) ^ { ^ }
    let bad = disk.inspect(handle.path ++ ".objects",filename)^
    disk.install(handle.path ++ ".objects",filename,original.bytes,bad.token)^
    let verified = local.verify(handle)^
    let checks = [len([for (entry in before where ends_with(entry.name,".pack")^) entry])^ == 1,
        *checked,interrupted is error,recovered,retained,rejected is error,verified.verified,
        unlocked_kept,cleaned_scratch,preserved]
    io.delete(area)^
    return checks
}
