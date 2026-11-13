// a crypto-suite change must reject old trust material without replacing local state (D11.2.3).
import identity: lambda.sync.peer_identity
import session: lambda.sync.peer_session
import crypto: lambda.io.crypto
import sqlite: lambda.io.sqlite
import codec: lambda.sync.codec
pn main() {
    let directory = "temp/sync-crypto-migration-" ++ string(sys.proc.self.pid#)
    io.mkdir(directory)^
    let current = identity.create("migration",crypto.key()^)^
    let legacy = {*:current,version:1}
    let wrap = crypto.key()^
    let box = crypto.seal(wrap,codec.encode(legacy)^,"lambda-peer-credentials-1")^
    open tx = (sqlite.create(directory ++ "/device.sqlite")^) {
        sqlite.execute(tx,"CREATE TABLE credentials (id INTEGER PRIMARY KEY CHECK (id = 1), box BLOB NOT NULL)")^
        sqlite.execute(tx,"INSERT INTO credentials VALUES (1, ?)",[box])^
    }
    let file = directory ++ "/retained-history"
    output("keep saved versions",file)^
    let load = identity.load(directory,wrap) ^ { ^ }
    let update = identity.update(directory,wrap,(value)=>current) ^ { ^ }
    let replace = identity.save(directory,wrap,current) ^ { ^ }
    var unchanged = false
    open tx = (sqlite.existing(directory ++ "/device.sqlite")^) {
        unchanged = sqlite.query(tx,"SELECT box FROM credentials WHERE id = 1")^[0][0] == box
    }
    let old_device = identity.validate_device({*:current.device,version:1}) ^ { ^ }
    let old_protocol = session.start(current,'client',"lambda-peer-lan-1") ^ { ^ }
    let retained = input(file,'text')^ == "keep saved versions"
    io.delete(directory)^
    return [current.version == 2,current.device.version == 2,identity.validate(current)^,
        load is error,load.message == "sync peer: legacy credentials require explicit re-enrollment; existing data preserved",
        update is error,replace is error,unchanged,retained,old_device is error,old_protocol is error]
}
