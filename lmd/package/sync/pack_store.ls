// SQLite holds disposable locators; pack bytes become durable before the transaction commits.
import sqlite: lambda.io.sqlite
import disk: lambda.io.disk
import wire: .object_wire
import pack: .pack
pub pn prepare(tx) any^ {
    sqlite.execute(tx,"CREATE TABLE IF NOT EXISTS pack_locations (id BLOB PRIMARY KEY, pack TEXT NOT NULL, offset INTEGER NOT NULL, size INTEGER NOT NULL) WITHOUT ROWID")^
    sqlite.execute(tx,"CREATE TABLE IF NOT EXISTS pack_pending (id BLOB PRIMARY KEY) WITHOUT ROWID")^
    sqlite.execute(tx,"CREATE TABLE IF NOT EXISTS pack_dirty (pack TEXT PRIMARY KEY) WITHOUT ROWID")^
    sqlite.execute(tx,"CREATE TABLE IF NOT EXISTS pack_obsolete (pack TEXT PRIMARY KEY) WITHOUT ROWID")^
}
pub pn pending(keys, id) any^ { return keys.tx != null and
    len(sqlite.query(keys.tx,"SELECT id FROM pack_pending WHERE id = ?",[id])^)^ > 0 }
pub pn location(keys, id) any^ {
    if (keys.tx == null) { return null }
    let rows = sqlite.query(keys.tx,"SELECT pack, offset, size FROM pack_locations WHERE id = ?",[id])^
    return if (len(rows)^ == 0) null else {pack:rows[0][0],offset:rows[0][1],size:rows[0][2]}
}
pub pn read_range(root, name, offset, size) any^ {
    let result = disk.read_stream(root,name,{offset:offset,length:size,max_bytes:pack.maximum,
        block_bytes:1048576,state:null},pn (acc,bytes)=> { return acc ++ bytes })^
    return result.state
}
pub pn read(keys, id) any^ {
    if (pending(keys,id)^) {
        let row = sqlite.query(keys.tx,"SELECT kind, box FROM objects WHERE id = ?",[id])^[0]
        return wire.encode({id:id,kind:row[0],box:row[1]})^
    }
    let found = location(keys,id)^
    return if (found == null) null else read_range(keys.objects,found.pack ++ ".pack",found.offset,found.size)^
}
pub pn present(keys, id) any^ {
    if (pending(keys,id)^) { return true }
    let found = location(keys,id)^
    return found != null and disk.inspect(keys.objects,found.pack ++ ".pack")^ != null
}
pub pn persist(tx, keys, records) any^ {
    let encoded = pack.encode(keys,records)^
    let name = encoded.id ++ ".pack"
    if (disk.inspect(keys.objects,name)^ == null) {
        let written:any^ = disk.install(keys.objects,name,encoded.bytes,null)
        if (written is error) {
            // Uncertain publication is accepted only after complete authenticated readback.
            let existing = disk.capture(keys.objects,name,pack.maximum)^.bytes
            pack.decode(keys,existing,encoded.id)^
        }
    } else { pack.decode(keys,disk.capture(keys.objects,name,pack.maximum)^.bytes,encoded.id)^ }
    for (entry in encoded.entries) {
        sqlite.execute(tx,"INSERT OR REPLACE INTO pack_locations VALUES (?, ?, ?, ?)",
            [entry.id,encoded.id,entry.offset,entry.length])^
        sqlite.execute(tx,"DELETE FROM pack_pending WHERE id = ?",[entry.id])^
    }
    return encoded.id
}
pub pn flush(tx, keys) any^ {
    repack(tx,keys)^
    let rows = sqlite.query(tx,"SELECT o.id, o.kind, o.box FROM pack_pending p JOIN objects o ON p.id = o.id ORDER BY o.id")^
    var records = []
    var size = 4
    for (row in rows) {
        let record = {id:row[0],kind:row[1],box:row[2]}
        let length = len(record.box)^+37
        if (len(records)^ > 0 and (size+length > pack.target or len(records)^ == pack.count_limit)) {
            persist(tx,keys,records)^
            records = []; size = 4
        }
        records = [*records,record]; size = size+length
    }
    if (len(records)^ > 0) { persist(tx,keys,records)^ }
    return true
}
pub pn stage(tx, keys, id) any^ {
    sqlite.execute(tx,"INSERT OR IGNORE INTO pack_pending VALUES (?)",[id])^
    let size = sqlite.query(tx,"SELECT COALESCE(SUM(length(o.box) + 37),0), COUNT(*) FROM pack_pending p JOIN objects o ON p.id = o.id")^[0]
    if (size[0] >= pack.target or size[1] >= pack.count_limit) { flush(tx,keys)^ }
    return id
}
pub pn remove(tx, keys, id) any^ {
    let found = location(keys,id)^
    sqlite.execute(tx,"DELETE FROM pack_pending WHERE id = ?",[id])^
    sqlite.execute(tx,"DELETE FROM pack_locations WHERE id = ?",[id])^
    if (found == null) { return true }
    sqlite.execute(tx,"INSERT OR IGNORE INTO pack_dirty VALUES (?)",[found.pack])^
    return true
}
pn repack(tx, keys) any^ {
    for (row in sqlite.query(tx,"SELECT pack FROM pack_dirty")^) {
        let id = row[0]
        let physical = disk.inspect(keys.objects,id ++ ".pack")^
        let live = sqlite.query(tx,"SELECT id FROM pack_locations WHERE pack = ?",[id])^
        if (physical == null and len(live)^ > 0) raise error("sync pack: live pack missing during pruning")
        if (physical != null) {
            let records = pack.decode(keys,disk.capture(keys.objects,id ++ ".pack",pack.maximum)^.bytes,id)^
            let ids = [for (entry in live) entry[0]]
            let retained = [for (record in records where contains(ids,record.id)^) record]
            if (len(retained)^ > 0) { persist(tx,keys,retained)^ }
        }
        sqlite.execute(tx,"INSERT OR IGNORE INTO pack_obsolete VALUES (?)",[id])^
        sqlite.execute(tx,"DELETE FROM pack_dirty WHERE pack = ?",[id])^
    }
    return true
}

pub pn cleanup(tx, keys) any^ {
    for (row in sqlite.query(tx,"SELECT pack FROM pack_obsolete")^) {
        if (len(sqlite.query(tx,"SELECT id FROM pack_locations WHERE pack = ? LIMIT 1",[row[0]])^)^ > 0)
            raise error("sync pack: obsolete pack still referenced")
        let name = row[0] ++ ".pack"
        let entry = disk.inspect(keys.objects,name)^
        if (entry != null) { disk.erase(keys.objects,name,entry.token)^ }
        sqlite.execute(tx,"DELETE FROM pack_obsolete WHERE pack = ?",[row[0]])^
    }
    return true
}
