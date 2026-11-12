// page roots carry actual physical keys; retained ciphertext does not need an epoch-wide recopy.
import transport: .cloud_transport
import pages: .metadata_pages
import schema: .cloud_schema
import codec: .codec
import catalog: .cloud_catalog
import hex: lambda.security.tuf.metadata

pub fn reader(config, credentials) => {get:pn (key)=> {
    return {body:transport.read(config,credentials,key)^}
}}
fn checked(value, root, epoch) any^ {
    if (value.root != root or value.id != schema.digest({version:2,epoch:epoch,root:root})^)
        raise error("sync cloud: service receipt does not bind the selected keys")
    return value
}
pub pn stage(config, credentials, keys, epoch) any^ {
    let ordered = sort(unique(keys))^
    if (len(ordered)^ == 0) { return null }
    var level = []
    for (offset in range(0,len(ordered)^,64)) {
        let selected = slice(ordered,offset,offset+64)^
        let root = pages.describe(pages.leaf_value([for (key in selected) {id:key,value:null}])^)^
        level = [*level,checked(transport.call(config,credentials,"stage",{keys:selected})^,root,epoch)^]
    }
    while (len(level)^ > 1) {
        var parents = []
        for (offset in range(0,len(level)^,32)) {
            let selected = slice(level,offset,offset+32)^
            let root = pages.combined([for (child in selected) child.root])^
            parents = [*parents,checked(transport.call(config,credentials,"stage_merge",
                {receipts:[for (child in selected) child.id]})^,root,epoch)^]
        }
        level = parents
    }
    return level[0]
}
pub pn merge(config, credentials, values, epoch) any^ {
    let receipts = [for (value in values where value != null) value.id]
    if (len(receipts)^ == 1) { return [for (value in values where value != null) value][0] }
    let root = pages.combined([for (value in values where value != null) value.root])^
    return checked(transport.call(config,credentials,"stage_merge",{receipts:receipts})^,root,epoch)^
}
pub pn expand(config, credentials, root) any^ {
    let entries = pages.all(reader(config,credentials),"",root,65536)^
    if (not all([for (entry in entries) starts_with(entry.id,"objects/")^ and schema.data_key(entry.id)^]))
        raise error("sync cloud: inventory contains non-object paths")
    let locations = [for (entry in entries) {id:hex.unhex(split(entry.id,"/")^[len(split(entry.id,"/")^)^-1])^,key:entry.id}]
    return {locations:locations,ids:sort(unique([for (entry in locations) entry.id]))^}
}
pub pn verify_binding(config, credentials, publication, root) any^ {
    let receipt = codec.decode(transport.read(config,credentials,"receipts/" ++ publication.dependencies)^,262144)^
    if (schema.digest(receipt)^ != publication.dependencies or receipt.version != 2 or
        receipt.epoch != catalog.epoch(publication.bundle.key)^)
        raise error("sync cloud: dependency receipt mismatch")
    let count = if (root == null) 0 else root.count
    let store = reader(config,credentials)
    if (receipt.root.count != count+1 or pages.lookup(store,"",receipt.root,publication.bundle.key)^ == null or
        (root != null and not pages.contains_root(store,"",receipt.root,root)^))
        raise error("sync cloud: inventory is not the signed dependency subtree")
    return true
}
