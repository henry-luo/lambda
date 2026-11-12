// provider locators are private encrypted pages; a physical path is evidence, never a logical content ID.
import repo: .repository
import pages: .metadata_pages
import codec: .codec
import schema: .cloud_schema
import v: .schema
import hex: lambda.security.tuf.metadata

pub fn roots(current) => if (current.cloud_locations == null) [] else current.cloud_locations
pub fn physical_id(keys, root) any^ => repo.object_id(keys,"locator",hex.unhex(root.id)^)^
pn read(tx, keys, key) any^ {
    let id = repo.object_id(keys,"locator",hex.unhex(slice(key,6)^)^)^
    return if (not repo.present(keys,id)^) null else {body:codec.encode(repo.get(tx,keys,id,"locator")^)^}
}
pn write(tx, keys, key, bytes, expected) any^ {
    if (expected != null or not starts_with(key,"pages/")^) raise error("sync locations: immutable page required")
    let value = codec.decode(bytes,262144)^
    if (schema.digest(value)^ != slice(key,6)^) raise error("sync locations: page ID mismatch")
    repo.store_object(tx,keys,"locator",value)^
    return {written:true}
}
pub fn adapter(tx, keys) => {get:pn (key)=> { return read(tx,keys,key)^ },
    write:pn (key,bytes,expected)=> { return write(tx,keys,key,bytes,expected)^ }}
pub pn update(tx, keys, current, provider, locations, replace=false) any^ {
    let previous = v.lookup(roots(current),provider)^
    let initial = if (previous == null or replace) null else previous.root
    let entries = [for (entry in locations) {id:hex.hex(entry.id)^,value:entry.key}]
    let root = pages.upsert_many(adapter(tx,keys),"",initial,entries)^
    return {*:current,cloud_locations:[*[for (entry in roots(current) where entry.id != provider) entry],
        {id:provider,root:root}]}
}
pub pn lookup(tx, keys, current, provider, id) any^ {
    let selected = v.lookup(roots(current),provider)^
    if (selected == null) { return null }
    let found = pages.lookup(adapter(tx,keys),"",selected.root,hex.hex(id)^)^
    return if (found == null) null else found.value
}
pub fn links(keys, value) any^ => if (value.kind == 'leaf') [] else
    [for (root in value.children) physical_id(keys,root)^]
pub pn verify(tx, keys, current) any^ {
    for (provider in roots(current)) {
        let entries = pages.all(adapter(tx,keys),"",provider.root,65536)^
        for (entry in entries) {
            if (not schema.id(entry.id)^ or not (entry.value is string) or
                not starts_with(entry.value,"objects/")^ or not schema.data_key(entry.value)^ or
                not ends_with(entry.value,"/" ++ entry.id)^)
                raise error("sync locations: invalid object location")
        }
    }
    return true
}
