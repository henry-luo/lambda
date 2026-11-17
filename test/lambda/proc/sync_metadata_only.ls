import local: lambda.sync.local
import repo: lambda.sync.repository
import packs: lambda.sync.pack_store
import sqlite: lambda.io.sqlite
import disk: lambda.io.disk
import crypto: lambda.io.crypto
import identity: lambda.sync.peer_identity
import codec: lambda.sync.codec
pn main() {
    let area = "temp/sync-metadata-only-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/tree")^
    let secret = crypto.key()^
    let handle = local.create(area ++ "/repo",secret,"metadata-only",area ++ "/tree","device")^
    output("text",area ++ "/tree/file")^
    let saved = local.scan(handle)^
    // synthesize the previous format to exercise authenticated migration, not just a fresh schema.
    let legacy = repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let records = [for (row in sqlite.query(tx,"SELECT id FROM objects")^) repo.record(k,row[0])^]
        let head = sqlite.query(tx,"SELECT generation, object FROM heads WHERE name = 'local'")^[0]
        return {records:records,generation:head[0],box:disk.capture(k.objects,head[1] ++ ".head",1048576)^.bytes}
    })^
    open tx = (sqlite.existing(handle.path)^) {
        sqlite.execute(tx,"ALTER TABLE objects ADD COLUMN box BLOB")^
        for (record in legacy.records) {
            sqlite.execute(tx,"UPDATE objects SET box = ? WHERE id = ?",[record.box,record.id])^
        }
        sqlite.execute(tx,"DROP TABLE heads")^
        sqlite.execute(tx,"CREATE TABLE heads (name TEXT PRIMARY KEY, generation INTEGER NOT NULL, box BLOB NOT NULL)")^
        sqlite.execute(tx,"INSERT INTO heads VALUES ('local', ?, ?)",[legacy.generation,legacy.box])^
        sqlite.execute(tx,"DROP TABLE pack_pending")^
        sqlite.execute(tx,"CREATE TABLE pack_pending (id BLOB PRIMARY KEY)")^
    }
    let reopened = local.open_repository(handle.path,secret,"metadata-only")^
    let checked = local.status(reopened)^
    let schema = repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let definitions = sqlite.query(tx,"SELECT sql FROM sqlite_master WHERE type = 'table'")^
        let id = repo.store_object(tx,k,"chunk_raw",binary("discard")^)^
        repo.remove_object(tx,k,id)^
        return all([for (row in definitions) not contains(row[0],"box")^])
    })^
    let directory = area ++ "/identity"
    io.mkdir(directory)^
    let wrapping = crypto.key()^
    let credentials = identity.create("metadata-only",secret)^
    open tx = (sqlite.create(directory ++ "/device.sqlite")^) {
        sqlite.execute(tx,"CREATE TABLE credentials (id INTEGER PRIMARY KEY, box BLOB NOT NULL)")^
        let box = crypto.seal(wrapping,codec.encode(credentials)^,"lambda-peer-credentials-1")^
        sqlite.execute(tx,"INSERT INTO credentials VALUES (1, ?)",[box])^
    }
    let migrated = identity.load(directory,wrapping)^
    let credential_schema = sqlite.query(directory ++ "/device.sqlite",
        "SELECT sql FROM sqlite_master WHERE name = 'credentials'")^[0][0]
    let checks = [checked.id == saved.id,checked.generation == saved.generation,schema,
        local.verify(reopened)^.verified,migrated == credentials,not contains(credential_schema,"box")^]
    io.delete(area)^
    return all(checks)
}
