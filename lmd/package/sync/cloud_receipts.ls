// only the service writes receipts; their epoch binds every bounded dependency check.
import pages: .metadata_pages
import schema: .cloud_schema
import codec: .codec
import v: .schema
import catalog: .cloud_catalog

pub pn read(store, prefix, id) any^ {
    if (not schema.id(id)) raise error("sync cloud: invalid receipt ID")
    let found = store.get(prefix ++ "receipts/" ++ id)^
    if (found == null) raise error("sync cloud: missing receipt")
    let value = codec.decode(found.body,262144)^
    if (schema.digest(value)^ != id) raise error("sync cloud: receipt digest mismatch")
    if (value.version == 1) { v.fields(value,["version","keys"])^ }
    else {
        v.fields(value,["version","epoch","root"])^
        if (value.version != 2 or not v.natural(value.epoch)^) raise error("sync cloud: invalid receipt")
        pages.valid_ref(value.root)^
    }
    return value
}
pn persist(store, prefix, value) any^ {
    let id = schema.digest(value)^
    let result = store.write(prefix ++ "receipts/" ++ id,codec.encode(value)^,null)^
    if (not result.written and read(store,prefix,id)^ != value) raise error("sync cloud: receipt collision")
    return {id:id,root:value.root}
}
pub pn stage(store, prefix, keys, epoch) any^ {
    let root = pages.build(store,prefix,[for (key in sort(unique(keys))^) {id:key,value:null}])^
    return persist(store,prefix,{version:2,epoch:epoch,root:root})^
}
pub pn merge(store, prefix, ids, epoch) any^ {
    if (not (ids is array) or len(ids)^ < 1 or len(ids)^ > 32) raise error("sync cloud: invalid receipt fanout")
    var roots = []
    for (id in ids) {
        let child = read(store,prefix,id)^
        if (child.version != 2 or child.epoch != epoch) raise error("sync cloud: stale receipt epoch")
        roots = [*roots,child.root]
    }
    let root = pages.concat(store,prefix,sort(roots,(entry)=>entry.first)^)^
    return persist(store,prefix,{version:2,epoch:epoch,root:root})^
}
pub pn publication(store, prefix, body, epoch) any^ {
    if (body.version != 2 or len(body.receipts)^ != 1 or body.receipts[0] != body.dependencies or
        catalog.epoch(body.bundle.key)^ != epoch)
        raise error("sync cloud: expected a paged dependency commitment")
    let receipt = read(store,prefix,body.dependencies)^
    if (receipt.version != 2 or receipt.epoch != epoch or receipt.root.count > 65537 or
        pages.lookup(store,prefix,receipt.root,body.bundle.key)^ == null)
        raise error("sync cloud: incomplete paged publication")
    return receipt.root
}
