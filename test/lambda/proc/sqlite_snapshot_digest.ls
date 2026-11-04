// Snapshot digests compare a clone with its source and reject modified scopes.
import sqlite: lambda.io.sqlite

pn show(value) { print(value); print("\n") }

pn read_digest(path) any^ {
    var digest=null
    open tx = (sqlite.existing(path)^) { digest=sqlite.snapshot_digest(tx)^ }
    return digest
}

pn modified_digest(path) any^ {
    open tx = (sqlite.existing(path)^) {
        sqlite.execute(tx,"INSERT INTO entries VALUES (2)")^
        sqlite.snapshot_digest(tx)^
    }
    return null
}

pn wrong_handle(path) any^ {
    open tx = (sqlite.existing(path)^) { sqlite.snapshot_digest(null)^ }
    return null
}

pn main() {
    let id=uuid()^
    let source="temp/sqlite-digest-source-" ++ id ++ ".db"
    let copy="temp/sqlite-digest-copy-" ++ id ++ ".db"
    let preimage="temp/sqlite-digest-preimage-" ++ id ++ ".db"
    open tx = (sqlite.create(source)^) {
        sqlite.execute(tx,"CREATE TABLE entries(value INTEGER NOT NULL)")^
        sqlite.execute(tx,"INSERT INTO entries VALUES (1)")^
    }
    // A separately committed change can alter SQLite's physical header on backup.
    sqlite.execute(source,"INSERT INTO entries VALUES (4)")^
    let original=read_digest(source)^
    show(len(original)==32)
    var snapshot=null
    open tx = (sqlite.clone(source,copy)^) {
        snapshot=sqlite.snapshot_digest(tx)^
        sqlite.preimage(tx,preimage)^
    }
    show((read_digest(copy)^)==snapshot and (read_digest(preimage)^)==snapshot)^
    sqlite.execute(copy,"INSERT INTO entries VALUES (3)")^
    show((read_digest(copy)^)!=snapshot)^
    let late: any^ = modified_digest(source)
    show(late is error and (sqlite.query(source,"SELECT COUNT(*) FROM entries")^)[0][0]==2)^
    let wrong: any^ = wrong_handle(source)
    let inactive: any^ = sqlite.snapshot_digest(null)
    show(wrong is error and inactive is error)
    io.delete(source)^; io.delete(copy)^; io.delete(preimage)^
}
