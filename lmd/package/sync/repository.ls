// portable authenticated objects; SQLite is only the local transactional locator.
import sqlite: lambda.io.sqlite
import crypto: lambda.io.crypto
import disk: lambda.io.disk
import hex: lambda.security.tuf.metadata
import codec: .codec
import chunks: .chunks
import blob: .blob
import v: .schema
import owner: .task_owner
import wire: .object_wire
import pack: .pack
import packs: .pack_store
let kinds = wire.kinds

pub pn access(path, keys, action) any^ {
    if (keys.owner != null and keys.owner != self()) {
        return owner.cached(keys.owner,pn (cache)=> { return access(path,{*:keys,cached_head:cache},action)^ })^
    }
    return disk.spool(path ++ ".objects",pack.maximum,pn (data)=> {
        return disk.spool(path ++ ".objects",pack.maximum,pn (shared)=> {
            return disk.spool(path ++ ".objects",pack.maximum,pn (private)=> {
                open tx = (sqlite.existing(path)^) {
                    let scoped = {*:keys,objects:path ++ ".objects",tx:tx,
                        spools:{data:data,shared:shared,private:private}}
                    packs.prepare(tx)^
                    migrate(tx,scoped)^
                    packs.cleanup(tx,scoped)^
                    let result = action(tx,scoped)^
                    packs.flush(tx,scoped)^
                    return result
                }
            })^
        })^
    })^
}

pub fn keys(secret, space, path=null) any^ {
    if (not (secret is binary) or len(secret) != 32 or not v.actor(space)^)
        raise error("sync repository: invalid key or space")
    return {space:space,objects:if (path == null) null else path ++ ".objects",
        encryption:crypto.mac(secret,"lambda-repository-1:encryption:" ++ space)^,
        identity:crypto.mac(secret,"lambda-repository-1:identity:" ++ space)^}
}
fn context(keys, kind, id) any^ => if (kind == "chunk_raw")
    binary("lambda-repository-chunk-raw-1:")^ ++ binary(keys.space)^ ++ id
    else codec.encode(["lambda-repository-1",keys.space,kind,id])^
pub fn object_id(keys, kind, digest) any^ => crypto.mac(keys.identity,context(keys,kind,digest)^)^
pub fn identity(keys, kind, value) any^ => object_id(keys,kind,codec.digest(value)^)^

fn authenticated(keys, record, source="object") any^ {
    v.fields(record,["id","kind","box"]) ^ { raise error("sync repository: " ++ source ++ " invalid physical record") }
    if (not (record.id is binary) or len(record.id) != 32 or
        not contains(kinds,record.kind)^)
        raise error("sync repository: invalid object header")
    let bytes = crypto.open(keys.encryption,record.box,context(keys,record.kind,record.id)^)^
    if (object_id(keys,record.kind,hash(bytes,'sha256')^)^ != record.id) raise error("sync repository: object identity mismatch")
    return bytes
}
pub fn is_chunk(kind) => kind == "chunk" or kind == "chunk_raw"
fn object_plaintext(kind, value) any^ => if (kind == "chunk_raw") binary(value)^ else codec.encode(value)^
fn decode(keys, record) any^ => if (record.kind == "chunk_raw") authenticated(keys,record,"decode raw")^ else
    codec.decode(authenticated(keys,record,"decode metadata")^)^
pub fn validate_record(keys, record) any^ {
    authenticated(keys,record)^
    return true
}

fn filename(id) any^ {
    if (not (id is binary) or len(id) != 32) raise error("sync repository: invalid object ID")
    return hex.hex(id)^ ++ ".object"
}
pn read_record(keys, id) any^ {
    let packed = packs.read(keys,id)^
    let bytes = if (packed == null) disk.capture(keys.objects,filename(id)^,wire.maximum)^.bytes else packed
    let found = wire.decode(bytes)^
    if (found.id != id) raise error("sync repository: object filename mismatch")
    return {record:found,plaintext:authenticated(keys,found,"read")^}
}
pub pn record(keys, id) any^ {
    return read_record(keys,id)^.record
}
pub pn present(keys, id) any^ { return packs.present(keys,id)^ or disk.inspect(keys.objects,filename(id)^)^ != null }
pub pn get(tx, keys, id, kind=null) any^ {
    let stored = read_record(keys,id)^
    if (kind != null and stored.record.kind != kind) raise error("sync repository: object kind mismatch")
    return if (stored.record.kind == "chunk_raw") stored.plaintext else codec.decode(stored.plaintext)^
}

pn index_record(tx, chosen) any^ {
    let rows = sqlite.query(tx,"SELECT kind FROM objects WHERE id = ?",[chosen.id])^
    if (len(rows) == 0) { sqlite.execute(tx,"INSERT INTO objects VALUES (?, ?)",[chosen.id,chosen.kind])^ }
    else if (rows[0][0] != chosen.kind) raise error("sync repository: locator index mismatch")
    return chosen.id
}

pn persist_record(tx, keys, proposed) any^ {
    let plaintext = authenticated(keys,proposed,"persist")^
    if (present(keys,proposed.id)^) {
        let existing = read_record(keys,proposed.id)^
        if (existing.record.kind != proposed.kind or existing.plaintext != plaintext)
            raise error("sync repository: immutable ID reused")
        return index_record(tx,existing.record)^
    }
    index_record(tx,proposed)^
    if (keys.tx != null) { return packs.stage(tx,keys,proposed)^ }
    // Direct initialization remains compatible with legacy callers outside a repository scope.
    let encoded = wire.encode(proposed)^
    disk.install(keys.objects,filename(proposed.id)^,encoded,null)^
    return proposed.id
}

pub pn store_object(tx, keys, kind, value) any^ {
    let plaintext = object_plaintext(kind,value)^
    let id = object_id(keys,kind,hash(plaintext,'sha256')^)^
    if (present(keys,id)^) {
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
    packs.prepare(tx)^
    sqlite.execute(tx,"CREATE TABLE objects (id BLOB PRIMARY KEY NOT NULL, kind TEXT NOT NULL) WITHOUT ROWID")^
    sqlite.execute(tx,"CREATE TABLE heads (name TEXT PRIMARY KEY NOT NULL, generation INTEGER NOT NULL, object TEXT NOT NULL) WITHOUT ROWID")^
    return null
}

pn store_head(keys, bytes) any^ {
    let id = hex.hex(hash(bytes,'sha256')^ )^
    let name = id ++ ".head"
    if (disk.inspect(keys.objects,name)^ == null) { disk.install(keys.objects,name,bytes,null)^ }
    else if (head_bytes(keys,id)^ != bytes) raise error("sync repository: head identity collision")
    return id
}
pn head_bytes(keys, id) any^ {
    if (not (id is string) or len(id)^ != 64) raise error("sync repository: invalid head pointer")
    let bytes = disk.capture(keys.objects,id ++ ".head",1048576)^.bytes
    if (hex.hex(hash(bytes,'sha256')^)^ != id) raise error("sync repository: head identity mismatch")
    return bytes
}
// old payload columns are read one record at a time and checked against durable files.
pn migrate(tx, keys) any^ {
    let definition = sqlite.query(tx,"SELECT sql FROM sqlite_master WHERE type = 'table' AND name = 'objects'")^[0][0]
    if (contains(definition,"box")^) {
        let rows = sqlite.query(tx,"SELECT id, kind FROM objects")^
        for (row in rows) {
            let stored = read_record(keys,row[0])^
            let old = sqlite.query(tx,"SELECT box FROM objects WHERE id = ?",[row[0]])^[0][0]
            if (stored.record.kind != row[1] or stored.record.box != old)
                raise error("sync repository: invalid legacy index")
        }
        sqlite.execute(tx,"ALTER TABLE objects DROP COLUMN box")^
        sqlite.execute(tx,"DROP TABLE IF EXISTS pack_pending")^
        packs.prepare(tx)^
    }
    let head_definition = sqlite.query(tx,"SELECT sql FROM sqlite_master WHERE type = 'table' AND name = 'heads'")^[0][0]
    if (contains(head_definition,"box")^) {
        let row = sqlite.query(tx,"SELECT generation, box FROM heads WHERE name = 'local'")^[0]
        let box = row[1]
        crypto.open(keys.encryption,box,context(keys,"head",row[0])^)^
        let id = store_head(keys,box)^
        sqlite.execute(tx,"ALTER TABLE heads RENAME TO heads_legacy")^
        sqlite.execute(tx,"CREATE TABLE heads (name TEXT PRIMARY KEY NOT NULL, generation INTEGER NOT NULL, object TEXT NOT NULL) WITHOUT ROWID")^
        sqlite.execute(tx,"INSERT INTO heads VALUES ('local', ?, ?)",[row[0],id])^
        sqlite.execute(tx,"DROP TABLE heads_legacy")^
    }
    return true
}
pub pn load(tx, keys) any^ {
    let rows = sqlite.query(tx,"SELECT generation, object FROM heads WHERE name = 'local'")^
    if (len(rows) != 1) raise error("sync repository: missing local head")
    let generation = rows[0][0]
    let plaintext = crypto.open(keys.encryption,head_bytes(keys,rows[0][1])^,context(keys,"head",generation)^)^
    let value = codec.decode(plaintext)^
    let id = value.state
    // The encrypted head authenticates the immutable ID before any in-memory reuse.
    let cached = keys.cached_head
    if (cached != null and cached.generation == generation and cached.id == id) { return cached }
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
    let head_id = store_head(keys,box)^
    if (generation == 0) { sqlite.execute(tx,"INSERT INTO heads VALUES ('local', 1, ?)",[head_id])^ }
    else {
        sqlite.execute(tx,"UPDATE heads SET generation = ?, object = ? WHERE name = 'local' AND generation = ?",
            [generation+1,head_id,generation])^
        if (sqlite.query(tx,"SELECT changes()")^[0][0] != 1) raise error("sync repository: stale generation")
    }
    return {generation:generation+1,id:id,state:current}
}

// A legacy inline manifest is bounded to one page worth of minimum-sized chunks.
pub fn legacy_capture_limit(config) => min(262144,config.minimum * 256)

pn store_chunks(tx, keys, pieces, kind="chunk") any^ {
    return [for (piece in pieces) {id:store_object(tx,keys,kind,piece)^,size:len(piece)^}]
}
pub pn content(tx, keys, bytes, config=chunks.default_profile) any^ {
    if (bytes != null and not (bytes is binary)) raise error("sync repository: expected binary content")
    let references = store_chunks(tx,keys,chunks.split(bytes,config)^,
        if (config.algorithm == chunks.default_profile.algorithm) "chunk_raw" else "chunk")^
    let digest = hash(bytes,'sha256')^
    let manifest = store_object(tx,keys,"manifest",{version:1,profile:config,size:len(bytes),hash:digest,chunks:references})^
    return blob.reference(manifest,len(bytes),digest)^
}
pn stream_chunks(tx, keys, acc, bytes, final=false) any^ {
    let fed = chunks.feed(acc.chunker,bytes,final)^
    var pending = [*acc.pending,*store_chunks(tx,keys,fed.pieces,"chunk_raw")^]
    var pages = acc.pages
    while (len(pending)^ >= 256 or (final and len(pending)^ > 0)) {
        let selected = take(pending,256)
        let id = store_object(tx,keys,"file_page",{version:1,chunks:selected})^
        pages = [*pages,{id:id,count:len(selected)^,size:sum([for (item in selected) item.size])}]
        pending = slice(pending,len(selected)^)^
    }
    return {chunker:fed.state,pending:pending,pages:pages}
}
pub pn content_file(tx, keys, root, path, config=chunks.default_profile, maximum=268435456, token=null) any^ {
    let initial = {chunker:chunks.start(config)^,pending:[],pages:[]}
    let captured = disk.read_stream(root,path,{state:initial,max_bytes:maximum,token:token,block_bytes:1048576},
        pn (acc,bytes)=> { return stream_chunks(tx,keys,acc,bytes)^ })^
    let complete = stream_chunks(tx,keys,captured.state,null,true)^
    let manifest = store_object(tx,keys,"manifest",{version:2,profile:config,size:captured.source.size,
        hash:captured.hash,pages:complete.pages})^
    return {source:captured.source,content:blob.reference(manifest,captured.source.size,captured.hash)^}
}
fn validate_chunks(entries, maximum=8388608) any^ {
    if (not (entries is array)) raise error("sync repository: expected chunk descriptors")
    for (descriptor in entries) {
        v.fields(descriptor,["id","size"])^
        if (not (descriptor.id is binary) or len(descriptor.id)^ != 32 or not v.natural(descriptor.size)^ or
            descriptor.size < 1 or descriptor.size > maximum) raise error("sync repository: invalid chunk descriptor")
    }
    return true
}
pub fn content_links(kind, value) any^ {
    if (kind == "file_page") {
        v.fields(value,["version","chunks"])^
        if (value.version != 1 or len(value.chunks)^ < 1 or len(value.chunks)^ > 256)
            raise error("sync repository: invalid chunk page")
        validate_chunks(value.chunks)^
        return [for (chunk in value.chunks) chunk.id]
    }
    v.record(value,["version","profile","size","hash"],["chunks","pages"])^
    chunks.profile(value.profile)^
    if (value.version == 1) {
        v.fields(value,["version","profile","size","hash","chunks"])^
        validate_chunks(value.chunks,value.profile.maximum)^
        return [for (chunk in value.chunks) chunk.id]
    }
    if (value.version != 2 or not (value.pages is array)) raise error("sync repository: invalid manifest version")
    v.fields(value,["version","profile","size","hash","pages"])^
    for (page in value.pages) {
        v.fields(page,["id","count","size"])^
        if (not (page.id is binary) or len(page.id)^ != 32 or not v.natural(page.count)^ or
            page.count < 1 or page.count > 256 or not v.natural(page.size)^ or page.size < 1)
            raise error("sync repository: invalid chunk page reference")
    }
    return [for (page in value.pages) page.id]
}
pub pn content_manifest(tx, keys, reference) any^ {
    v.descriptor(reference)^
    let manifest = get(tx,keys,reference.manifest,"manifest")^
    content_links("manifest",manifest)^
    if (manifest.size != reference.size or manifest.hash != reference.hash)
        raise error("sync repository: manifest binding mismatch")
    var descriptors = if (manifest.version == 1) manifest.chunks else []
    if (manifest.version == 2) {
        for (page in manifest.pages) {
            let value = get(tx,keys,page.id,"file_page")^
            content_links("file_page",value)^
            if (len(value.chunks)^ != page.count or sum([for (item in value.chunks) item.size]) != page.size)
                raise error("sync repository: chunk page binding mismatch")
            descriptors = [*descriptors,*value.chunks]
        }
    }
    validate_chunks(descriptors,manifest.profile.maximum)^
    if (sum([0,*[for (descriptor in descriptors) descriptor.size]]) != manifest.size)
        raise error("sync repository: manifest length mismatch")
    return {*:manifest,chunks:descriptors}
}
pn content_chunk(tx, keys, id) any^ {
    let stored = read_record(keys,id)^
    if (not is_chunk(stored.record.kind)^) raise error("sync repository: expected chunk object")
    return if (stored.record.kind == "chunk_raw") stored.plaintext else codec.decode(stored.plaintext)^
}
pn content_next(tx, keys, descriptors, acc) any^ {
    if (acc.next == len(descriptors)^) { return {bytes:null,state:acc,done:true} }
    let descriptor = descriptors[acc.next]
    let bytes = content_chunk(tx,keys,descriptor.id)^
    if (not (bytes is binary) or len(bytes)^ != descriptor.size) raise error("sync repository: invalid chunk")
    return {bytes:bytes,state:{next:acc.next+1,
        bytes:if (acc.collect) acc.bytes ++ bytes else null,collect:acc.collect},done:false}
}
pub pn availability(tx, keys, reference, evicted=[], collect=true) any^ {
    let manifest = content_manifest(tx,keys,reference)^
    let missing = [for (descriptor in manifest.chunks where
        not present(keys,descriptor.id)^ and contains(evicted,descriptor.id)^) descriptor.id]
    if (len(missing)^ > 0) {
        for (descriptor in manifest.chunks where not contains(missing,descriptor.id)^) {
            let bytes = content_chunk(tx,keys,descriptor.id)^
            if (not (bytes is binary) or len(bytes)^ != descriptor.size) raise error("sync repository: invalid chunk")
        }
        return {complete:false,missing:missing,bytes:null}
    }
    let checked = crypto.hash_stream({size:reference.size,state:{next:0,bytes:binary([])^,collect:collect and reference.size <= 8388608}},
        pn (acc)=> { return content_next(tx,keys,manifest.chunks,acc)^ })^
    if (checked.hash != reference.hash) raise error("sync repository: reconstructed content mismatch")
    return {complete:true,missing:[],bytes:checked.state.bytes}
}
pub pn payload(tx, keys, reference) any^ {
    if (reference.size > 8388608) raise error("sync repository: use streaming content installation for large files")
    return availability(tx,keys,reference)^.bytes
}
pub pn install_content(tx, keys, reference, root, path, expected=null) any^ {
    if (reference.size <= legacy_capture_limit(chunks.default_profile)) { return disk.install(root,path,payload(tx,keys,reference)^,expected)^ }
    let manifest = content_manifest(tx,keys,reference)^
    disk.write_stream(root,path,{size:reference.size,hash:reference.hash,expected:expected,
        state:{next:0,bytes:null,collect:false}},pn (acc)=> { return content_next(tx,keys,manifest.chunks,acc)^ })^
    return null
}

pub pn inventory(tx) any^ {
    return [for (row in sqlite.query(tx,"SELECT id FROM objects ORDER BY id")^) row[0]]
}

pub pn export_objects(tx, keys, ids) any^ {
    if (not (ids is array) or len(ids) > 128) raise error("sync repository: object batch limit")
    var records = []
    var i = 0
    while (i < len(ids)) {
        let record = record(keys,ids[i])^
        if (contains(["state","maintenance","locator"],record.kind)^) raise error("sync repository: private local state cannot be exported")
        decode(keys,record)^
        records = [*records,record]
        i = i+1
    }
    return records
}

pub pn export_pack(tx, keys, ids) any^ {
    if (len(ids)^ > 0) {
        let located = packs.location(keys,ids[0])^
        if (located != null) {
            let bytes = disk.capture(keys.objects,located.pack ++ ".pack",pack.maximum)^.bytes
            let records = pack.decode(keys,bytes,located.pack)^
            // private local state never crosses the export boundary in a shared physical pack.
            if (all([for (record in records) not contains(["state","maintenance","locator"],record.kind)^])) {
                for (record in records) { authenticated(keys,record)^ }
                let have = [for (record in records) record.id]
                return {bytes:bytes,id:located.pack,ids:[for (id in ids where contains(have,id)^) id]}
            }
        }
    }
    var selected = []
    var size = 4
    var position = 0
    while (position < len(ids)^ and len(selected)^ < 128) {
        let record = export_objects(tx,keys,[ids[position]])^[0]
        let length = len(record.box)^+37
        if (len(selected)^ > 0 and size+length > pack.target) { break }
        selected = [*selected,record]; size = size+length; position = position+1
    }
    return {*:pack.encode(keys,selected)^,ids:[for (record in selected) record.id]}
}

pub pn import_objects(tx, keys, records) any^ {
    if (not (records is array) or len(records) > 128) raise error("sync repository: object batch limit")
    var i = 0
    while (i < len(records)) {
        let record = records[i]
        if (contains(["state","maintenance","locator"],record.kind)^) raise error("sync repository: private local state cannot be imported")
        decode(keys,record)^
        persist_record(tx,keys,record)^
        i = i+1
    }
    return true
}

// synchronous removal stays inside the caller's SQLite writer scope; retry after a crash is idempotent.
pub pn remove_object(tx, keys, id) any^ {
    packs.remove(tx,keys,id)^
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
    var position = 0
    while (position < len(names)^) {
        let entry = names[position]
        position = position+1
        if (entry.kind != 'file') raise error("sync repository: unexpected repository entry")
        if (starts_with(entry.name,".lambda-spool-")^ or ends_with(entry.name,".head")^) { continue }
        if (len(entry.name)^ == 69 and ends_with(entry.name,".pack")^) {
            let id = slice(entry.name,0,64)^
            let retired = if (keys.tx == null) [] else sqlite.query(keys.tx,"SELECT pack FROM pack_obsolete WHERE pack = ?",[id])^
            if (len(retired)^ > 0) { continue }
            let stored = pack.decode(keys,disk.capture(keys.objects,entry.name,pack.maximum)^.bytes,id)^
            var physical_offset = 4
            for (item in stored) {
                authenticated(keys,item)^
                if (keys.tx != null) {
                    // Orphan recovery must locate authenticated blobs before rebuilding the object index.
                    sqlite.execute(keys.tx,"INSERT OR IGNORE INTO pack_locations VALUES (?, ?, ?, ?)",
                        [item.id,id,physical_offset,len(item.box)^+37])^
                }
                physical_offset = physical_offset+len(item.box)^+37
            }
            result = [*result,*[for (item in stored where not any([for (prior in result) prior.id == item.id])) item]]
            continue
        }
        if (len(entry.name)^ == 71 and ends_with(entry.name,".object")^) {
            let id = hex.unhex(slice(entry.name,0,64)^)^
            let stored = wire.decode(disk.capture(keys.objects,entry.name,wire.maximum)^.bytes)^
            if (stored.id != id or filename(id)^ != entry.name) raise error("sync repository: noncanonical object name")
            authenticated(keys,stored)^
            if (not any([for (prior in result) prior.id == id])) { result = [*result,stored] }
            continue
        }
        raise error("sync repository: unexpected repository entry")
    }
    if (keys.tx != null) {
        for (row in sqlite.query(keys.tx,"SELECT id FROM pack_pending")^) {
            let item = wire.decode(packs.read(keys,row[0])^)^
            authenticated(keys,item)^
            if (not any([for (prior in result) prior.id == item.id])) { result = [*result,item] }
        }
    }
    return result
}

pub pn rebuild_index(tx, keys) any^ {
    let stored = records(keys)^
    sqlite.execute(tx,"DELETE FROM pack_locations")^
    for (entry in disk.entries(keys.objects,"",100000)^ where ends_with(entry.name,".pack")^) {
        let id = slice(entry.name,0,64)^
        if (len(sqlite.query(tx,"SELECT pack FROM pack_obsolete WHERE pack = ?",[id])^)^ > 0) { continue }
        let bytes = disk.capture(keys.objects,entry.name,pack.maximum)^.bytes
        let n = wire.decode_u32(bytes,len(bytes)^-4)^
        let locations = pack.index(keys,slice(bytes,len(bytes)^-n-4)^,len(bytes)^)^
        for (location in locations) {
            sqlite.execute(tx,"INSERT OR REPLACE INTO pack_locations VALUES (?, ?, ?, ?)",
                [location.id,id,location.offset,location.length])^
        }
    }
    sqlite.execute(tx,"DELETE FROM objects")^
    var i = 0
    while (i < len(stored)) {
        let item = stored[i]
        sqlite.execute(tx,"INSERT INTO objects VALUES (?, ?)",[item.id,item.kind])^
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
        let indexed = sqlite.query(tx,"SELECT kind FROM objects WHERE id = ?",[item.id])^
        if (len(indexed) != 0 and (indexed[0][0] != item.kind))
            raise error("sync repository: locator index mismatch")
        if (item.kind == "manifest" and not contains(removing,item.id)^) {
            let value = decode(keys,item)^
            availability(tx,keys,blob.reference(item.id,value.size,value.hash)^,evicted,false)^
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
