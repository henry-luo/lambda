import pages: lambda.sync.metadata_pages
import fixture: .mod_sync_cloud_store
pn main() {
    let area = "temp/sync-metadata-batch-" ++ string(sys.proc.self.pid#)
    io.mkdir(area)^
    let store = fixture.create(area ++ "/store.sqlite")^
    let entries = [for (i in 0 to 2100) {id:"k" ++ string(10000+i*2),value:i}]
    let root = pages.build(store,"batch/",entries)^
    let updates = [{id:"a",value:1},{id:"z",value:2},
        *[for (i in 0 to 80) {id:"k" ++ string(10000+i*37),value:-i}],{id:"a",value:3}]
    let changed = pages.upsert_many(store,"batch/",root,updates)^
    var reference = root
    for (entry in sort(updates,(entry)=>entry.id)^) {
        reference = pages.upsert(store,"batch/",reference,entry.id,entry.value)^
    }
    let actual = pages.all(store,"batch/",changed)^
    let expected = pages.all(store,"batch/",reference)^
    let noop = pages.upsert_many(store,"batch/",root,slice(entries,60,140)^)^
    let empty = pages.upsert_many(store,"batch/",root,[])^
    let fresh = pages.upsert_many(store,"fresh/",null,updates)^
    let original = pages.all(store,"batch/",root)^
    let invalid = pages.upsert_many(store,"batch/",root,[{id:"",value:1}]) ^ { ^ }
    let checks = [actual == expected,noop == root,empty == root,original == entries,
        pages.lookup(store,"batch/",changed,"a")^.value == 3,
        pages.lookup(store,"batch/",changed,"z")^.value == 2,
        pages.all(store,"fresh/",fresh)^ == [for (entry in expected where
            any([for (update in updates) update.id == entry.id])) entry],invalid is error]
    io.delete(area)^
    return checks
}
