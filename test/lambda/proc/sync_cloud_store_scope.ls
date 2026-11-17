import fixture: .mod_sync_cloud_store
pn main() {
    let area = "temp/sync-fixture-scope-" ++ string(sys.proc.self.pid#)
    io.mkdir(area)^
    let path = area ++ "/store.sqlite"
    let durable = fixture.create(path)^
    let checked = fixture.scoped(path,pn (store)=> {
        let a = store.write("a","one",null)^
        let denied = store.write("a","wrong",null)^
        let changed = store.write("a","two",a.etag)^
        store.write("b","three",null)^
        let page = store.enumerate("",null,1)^
        let next = store.enumerate("",page.next,1)^
        return [a.written,not denied.written,changed.written,store.get("a")^.body == "two",
            store.head("a")^.etag == changed.etag,len(page.entries)^ == 1,next.entries[0].key == "b"]
    })^
    let rolled_back = fixture.scoped(path,pn (store)=> {
        store.write("c","discard",null)^
        store.erase("a")^
        raise error("rollback fixture")
    }) ^ { ^ }
    let expired = fixture.scoped(path,pn (store)=> { return store })^
    let escaped = expired.get("a") ^ { ^ }
    let persisted = durable.get("a")^
    let result = [all(checked),rolled_back is error,durable.get("c")^ == null,
        persisted.body == "two",escaped is error]
    io.delete(area)^
    return result
}
