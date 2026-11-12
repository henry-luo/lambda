import pages: lambda.sync.metadata_pages
import fixture: .mod_sync_cloud_store
import codec: lambda.sync.codec

pn main() {
    let area = "temp/sync-metadata-pages-" ++ string(sys.proc.self.pid#)
    io.mkdir(area)^
    let store = fixture.create(area ++ "/index.sqlite")^
    let entries = [for (index in 0 to 256) {id:"key-" ++ string(index+10000),value:index}]
    let root = pages.build(store,"repo/",entries)^
    let first = pages.page(store,"repo/",root,null,17)^
    let second = pages.page(store,"repo/",root,first.next,17)^
    let found = pages.lookup(store,"repo/",root,"key-10120")^
    let changed = pages.upsert(store,"repo/",root,"key-10120",777)^
    let inserted = pages.upsert(store,"repo/",changed,"key-10257",257)^
    let old = pages.lookup(store,"repo/",root,"key-10120")^
    let unchanged = pages.upsert(store,"repo/",root,"key-10120",120)^
    let stale = pages.page(store,"repo/",changed,first.next,17) ^ { ^ }
    let duplicate = pages.build(store,"repo/",[entries[0],entries[0]]) ^ { ^ }
    let overlap = pages.concat(store,"repo/",[root,root]) ^ { ^ }
    let missing = pages.lookup(store,"repo/",root,"absent")^
    let all = pages.all(store,"repo/",inserted)^
    var appended = null
    for (entry in slice(entries,0,100)^) { appended = pages.upsert(store,"append/",appended,entry.id,entry.value)^ }
    let monotonic = pages.all(store,"append/",appended)^
    let stored = store.get("repo/pages/" ++ root.id)^
    store.write("repo/pages/" ++ root.id,codec.encode({bad:true})^,stored.etag)^
    let corrupted = pages.page(store,"repo/",root) ^ { ^ }
    io.delete(area)^
    return [root.count == 257,len(first.entries)^ == 17,second.entries[0] == entries[17],
        found.value == 120,old.value == 120,pages.valid_ref(changed)^,inserted.count == 258,
        unchanged == root,stale is error,duplicate is error,overlap is error,missing == null,
        len(all)^ == 258,all[120].value == 777,all[257].value == 257,
        monotonic == slice(entries,0,100)^,corrupted is error]
}
