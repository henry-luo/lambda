// portable authenticated objects; SQLite is only the local transactional locator.
import sqlite: lambda.io.sqlite
import crypto: lambda.io.crypto
import disk: lambda.io.disk
import hex: lambda.security.tuf.metadata
import codec: .codec
import chunks: .chunks
import blob: .blob
import v: .schema
let kinds = ["chunk","manifest","state","snapshot","checkpoint","document","version","history","retention","maintenance","cache"]

pub pn access(path, keys, action) any^ {
    open tx = (sqlite.existing(path)^) { return action(tx,{*:keys,objects:path ++ ".objects"}) }
}

pub fn keys(secret, space, path=null) any^ {
    if (not (secret is binary) or len(secret) != 32 or not v.actor(space)^)
        raise error("sync repository: invalid key or space")
    return {space:space,objects:if (path == null) null else path ++ ".objects",
        encryption:crypto.mac(secret,"lambda-repository-1:encryption:" ++ space)^,
        identity:crypto.mac(secret,"lambda-repository-1:identity:" ++ space)^}
}
fn context(keys, kind, id) any^ => codec.encode(["lambda-repository-1",keys.space,kind,id])^
pub fn object_id(keys, kind, digest) any^ => crypto.mac(keys.identity,context(keys,kind,digest)^)^
pub fn identity(keys, kind, value) any^ => object_id(keys,kind,codec.digest(value)^)^

fn authenticated(keys, record) any^ {
    v.fields(record,["id","kind","box"])^
    if (not (record.id is binary) or len(record.id) != 32 or
        not contains(kinds,record.kind)^)
        raise error("sync repository: invalid object header")
    let bytes = crypto.open(keys.encryption,record.box,context(keys,record.kind,record.id)^)^
    if (object_id(keys,record.kind,hash(bytes,'sha256')^)^ != record.id) raise error("sync repository: object identity mismatch")
    return bytes
}
fn decode(keys, record) any^ => codec.decode(authenticated(keys,record)^)^
pub fn validate_record(keys, record) any^ {
    authenticated(keys,record)^
    return true
}

fn filename(id) any^ {
    if (not (id is binary) or len(id) != 32) raise error("sync repository: invalid object ID")
    return hex.hex(id)^ ++ ".object"
}
pn read_record(keys, id) any^ {
    let item = disk.capture(keys.objects,filename(id)^,8388608)^
    let bytes = item.bytes
    if (len(bytes)^ < 69 or slice(bytes,0,4)^ != binary("LRP1")^ or bytes[4] >= len(kinds))
        raise error("sync repository: invalid physical envelope")
    let found = {id:slice(bytes,5,37)^,kind:kinds[bytes[4]],box:slice(bytes,37,len(bytes)^)^}
    if (found.id != id) raise error("sync repository: object filename mismatch")
    return {record:found,plaintext:authenticated(keys,found)^}
}
pub pn record(keys, id) any^ {
    return read_record(keys,id)^.record
}
pub pn present(keys, id) any^ { return disk.inspect(keys.objects,filename(id)^)^ != null }
pub pn get(tx, keys, id, kind=null) any^ {
    let stored = read_record(keys,id)^
    if (kind != null and stored.record.kind != kind) raise error("sync repository: object kind mismatch")
    return codec.decode(stored.plaintext)^
}

pn index_record(tx, chosen) any^ {
    let rows = sqlite.query(tx,"SELECT kind, box FROM objects WHERE id = ?",[chosen.id])^
    if (len(rows) == 0) { sqlite.execute(tx,"INSERT INTO objects VALUES (?, ?, ?)",[chosen.id,chosen.kind,chosen.box])^ }
    else if (rows[0][0] != chosen.kind or rows[0][1] != chosen.box) raise error("sync repository: locator index mismatch")
    return chosen.id
}

pn persist_record(tx, keys, proposed) any^ {
    let plaintext = authenticated(keys,proposed)^
    let path = filename(proposed.id)^
    var chosen = proposed
    if (disk.inspect(keys.objects,path)^ == null) {
        // ciphertext stays opaque bytes instead of being expanded through the text codec.
        let encoded = binary("LRP1")^ ++ binary([index_of(kinds,proposed.kind)^])^ ++ proposed.id ++ proposed.box
        if (len(encoded)^ > 8388608) raise error("sync repository: physical object limit")
        let written:any^ = disk.install(keys.objects,path,encoded,null)
        // Another writer or a post-publication sync error requires authenticated readback.
        if (written is error and disk.inspect(keys.objects,path)^ == null) raise written
    }
    let existing = read_record(keys,proposed.id)^
    chosen = existing.record
    if (chosen.kind != proposed.kind or existing.plaintext != plaintext) raise error("sync repository: immutable ID reused")
    return index_record(tx,chosen)^
}

pub pn store_object(tx, keys, kind, value) any^ {
    let plaintext = codec.encode(value)^
    let id = object_id(keys,kind,hash(plaintext,'sha256')^)^
    if (disk.inspect(keys.objects,filename(id)^)^ != null) {
        let stored = read_record(keys,id)^
        if (stored.record.kind != kind or stored.plaintext != binary(plaintext)^) raise error("sync repository: immutable ID reused")
        return index_record(tx,stored.record)^
    }
    let box = crypto.seal(keys.encryption,plaintext,context(keys,kind,id)^)^
    return persist_record(tx,keys,{id:id,kind:kind,box:box})^
}

pub pn initialize(tx, keys) any^ {
    io.mkdir(keys.objects)^
    disk.resolve(keys.objects)^
    if (len(disk.entries(keys.objects,"",100000)^) != 0) raise error("sync repository: object directory must be empty at creation")
    io.chmod(keys.objects,"700")^
    sqlite.execute(tx,"CREATE TABLE objects (id BLOB PRIMARY KEY NOT NULL, kind TEXT NOT NULL, box BLOB NOT NULL) WITHOUT ROWID")^
    sqlite.execute(tx,"CREATE TABLE heads (name TEXT PRIMARY KEY NOT NULL, generation INTEGER NOT NULL, box BLOB NOT NULL) WITHOUT ROWID")^
    return null
}

pub pn load(tx, keys) any^ {
    let rows = sqlite.query(tx,"SELECT generation, box FROM heads WHERE name = 'local'")^
    if (len(rows) != 1) raise error("sync repository: missing local head")
    let generation = rows[0][0]
    let plaintext = crypto.open(keys.encryption,rows[0][1],context(keys,"head",generation)^)^
    let value = codec.decode(plaintext)^
    let id = value.state
    return {generation:generation,id:id,state:get(tx,keys,id,"state")^}
}

pub pn commit_head(tx, keys, generation, current) any^ {
    if (not v.natural(generation)^) raise error("sync repository: invalid generation")
    let rows = sqlite.query(tx,"SELECT generation FROM heads WHERE name = 'local'")^
    if ((generation == 0 and len(rows) != 0) or
        (generation != 0 and (len(rows) != 1 or rows[0][0] != generation)))
        raise error("sync repository: stale generation")
    let id = store_object(tx,keys,"state",current)^
    let box = crypto.seal(keys.encryption,codec.encode({state:id})^,context(keys,"head",generation+1)^)^
    if (generation == 0) { sqlite.execute(tx,"INSERT INTO heads VALUES ('local', 1, ?)",[box])^ }
    else {
        sqlite.execute(tx,"UPDATE heads SET generation = ?, box = ? WHERE name = 'local' AND generation = ?",
            [generation+1,box,generation])^
        if (sqlite.query(tx,"SELECT changes()")^[0][0] != 1) raise error("sync repository: stale generation")
    }
    return {generation:generation+1,id:id,state:current}
}

pub pn content(tx, keys, bytes, config=chunks.default_profile) any^ {
    if (bytes != null and not (bytes is binary)) raise error("sync repository: expected binary content")
    let pieces = chunks.split(bytes,config)^
    var references = []
    var i = 0
    while (i < len(pieces)) {
        let piece = pieces[i]
        references = [*references,{id:store_object(tx,keys,"chunk",piece)^,size:len(piece)}]
        i = i+1
    }
    let digest = hash(bytes,'sha256')^
    let manifest = store_object(tx,keys,"manifest",{version:1,profile:config,size:len(bytes),hash:digest,chunks:references})^
    return blob.reference(manifest,len(bytes),digest)^
}

pn content_manifest(tx, keys, reference) any^ {
    v.descriptor(reference)^
    let manifest = get(tx,keys,reference.manifest,"manifest")^
    v.fields(manifest,["version","profile","size","hash","chunks"])^
    chunks.profile(manifest.profile)^
    if (manifest.version != 1 or manifest.size != reference.size or manifest.hash != reference.hash or
        manifest.size > 8388608 or not (manifest.chunks is array)) raise error("sync repository: invalid manifest")
    for (descriptor in manifest.chunks) {
        v.fields(descriptor,["id","size"])^
        if (not (descriptor.id is binary) or len(descriptor.id)^ != 32 or not v.natural(descriptor.size)^ or
            descriptor.size < 1 or descriptor.size > manifest.profile.maximum) raise error("sync repository: invalid chunk descriptor")
    }
    if (sum([0,*[for (descriptor in manifest.chunks) descriptor.size]]) != manifest.size)
        raise error("sync repository: manifest length mismatch")
    return manifest
}
pub pn availability(tx, keys, reference, evicted=[]) any^ {
    let manifest = content_manifest(tx,keys,reference)^
    var bytes = binary([])^
    var missing = []
    for (descriptor in manifest.chunks) {
        if (not present(keys,descriptor.id)^ and contains(evicted,descriptor.id)^) {
            missing = [*missing,descriptor.id]
        } else {
            let piece = get(tx,keys,descriptor.id,"chunk")^
            if (not (piece is binary) or len(piece) != descriptor.size) raise error("sync repository: invalid chunk")
            bytes = bytes ++ piece
        }
    }
    if (len(missing) == 0 and not blob.verify(reference,bytes)^)
        raise error("sync repository: reconstructed content mismatch")
    return {complete:len(missing) == 0,missing:missing,bytes:if (len(missing) == 0) bytes else null}
}
pub pn payload(tx, keys, reference) any^ { return availability(tx,keys,reference)^.bytes }

pub pn inventory(tx) any^ {
    return [for (row in sqlite.query(tx,"SELECT id FROM objects ORDER BY id")^) row[0]]
}

pub pn export_objects(tx, keys, ids) any^ {
    if (not (ids is array) or len(ids) > 128) raise error("sync repository: object batch limit")
    var records = []
    var i = 0
    while (i < len(ids)) {
        let record = record(keys,ids[i])^
        if (contains(["state","maintenance"],record.kind)^) raise error("sync repository: private local state cannot be exported")
        decode(keys,record)^
        records = [*records,record]
        i = i+1
    }
    return records
}

pub pn import_objects(tx, keys, records) any^ {
    if (not (records is array) or len(records) > 128) raise error("sync repository: object batch limit")
    var i = 0
    while (i < len(records)) {
        let record = records[i]
        if (contains(["state","maintenance"],record.kind)^) raise error("sync repository: private local state cannot be imported")
        decode(keys,record)^
        persist_record(tx,keys,record)^
        i = i+1
    }
    return true
}

// synchronous removal stays inside the caller's SQLite writer scope; retry after a crash is idempotent.
pub pn remove_object(tx, keys, id) any^ {
    let path = filename(id)^
    let observed = disk.inspect(keys.objects,path)^
    if (observed != null) {
        read_record(keys,id)^
        disk.erase(keys.objects,path,observed.token)^
    }
    sqlite.execute(tx,"DELETE FROM objects WHERE id = ?",[id])^
    return true
}

pub pn records(keys) any^ {
    let names = sort(disk.entries(keys.objects,"",100000)^,(entry)=>entry.name)^
    var result = []
    var i = 0
    while (i < len(names)) {
        let entry = names[i]
        if (entry.kind != 'file' or len(entry.name)^ != 71 or not ends_with(entry.name,".object")^)
            raise error("sync repository: unexpected repository entry")
        let id = hex.unhex(slice(entry.name,0,64)^)^
        let stored = record(keys,id)^
        if (filename(id)^ != entry.name) raise error("sync repository: noncanonical object name")
        result = [*result,stored]
        i = i+1
    }
    return result
}
pub pn rebuild_index(tx, keys) any^ {
    let stored = records(keys)^
    sqlite.execute(tx,"DELETE FROM objects")^
    var i = 0
    while (i < len(stored)) {
        let item = stored[i]
        sqlite.execute(tx,"INSERT INTO objects VALUES (?, ?, ?)",[item.id,item.kind,item.box])^
        i = i+1
    }
    return {objects:len(stored),rebuilt:true}
}
pub pn recover(tx, keys) any^ {
    let indexed = inventory(tx)^
    return [for (item in records(keys)^ where not contains(indexed,item.id)^) {id:item.id,kind:item.kind}]
}
pub pn verify(tx, keys, evicted=[], removing=[]) any^ {
    let stored = records(keys)^
    let ids = inventory(tx)^
    var i = 0
    while (i < len(stored)) {
        let item = stored[i]
        let indexed = sqlite.query(tx,"SELECT kind, box FROM objects WHERE id = ?",[item.id])^
        if (len(indexed) != 0 and (indexed[0][0] != item.kind or indexed[0][1] != item.box))
            raise error("sync repository: locator index mismatch")
        if (item.kind == "manifest" and not contains(removing,item.id)^) {
            let value = decode(keys,item)^
            availability(tx,keys,blob.reference(item.id,value.size,value.hash)^,evicted)^
        }
        i = i+1
    }
    if (any([for (id in ids where not contains([*removing,*evicted],id)^) not any([for (item in stored) item.id == id])])) raise error("sync repository: indexed object missing")
    load(tx,keys)^
    let stored_ids = [for (item in stored) item.id]
    let missing = [for (id in evicted where not contains(stored_ids,id)^) id]
    return {objects:len(stored),unindexed:len([for (id in stored_ids where not contains(ids,id)^) id]),
        verified:true,data_complete:len(missing) == 0,evicted:len(missing),
        pending_removal:len([for (id in ids where contains(removing,id)^ and not contains(stored_ids,id)^) id])}
}
