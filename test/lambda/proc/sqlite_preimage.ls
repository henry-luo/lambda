// A clone preimage is a separate create-only effect before migration (S12.4.4v4, D11.2.2).
import sqlite: lambda.io.sqlite

pn create_source(path) any^ {
    open tx = (sqlite.create(path)^) {
        sqlite.execute(tx,"CREATE TABLE sample (n INTEGER)")^
        sqlite.execute(tx,"INSERT INTO sample VALUES (42)")^
    }
    return null
}
pn upgrade(source,dest,backup) any^ {
    open tx = (sqlite.clone(source,dest)^) {
        sqlite.query(tx,"SELECT n FROM sample")^
        sqlite.preimage(tx,backup)^
        sqlite.execute(tx,"INSERT INTO sample VALUES (43)")^
    }
    return null
}
pn existing_backup(source,dest,backup) any^ {
    open tx = (sqlite.clone(source,dest)^) {
        sqlite.preimage(tx,backup) ^ { null }
        sqlite.execute(tx,"INSERT INTO sample VALUES (99)")^
    }
    return null
}
pn late_backup(source,dest,backup) any^ {
    open tx = (sqlite.clone(source,dest)^) {
        sqlite.execute(tx,"INSERT INTO sample VALUES (99)")^
        sqlite.preimage(tx,backup) ^ { null }
    }
    return null
}
pn abort_after_backup(source,dest,backup) any^ {
    open tx = (sqlite.clone(source,dest)^) {
        sqlite.preimage(tx,backup)^
        raise error("cancel upgrade")
    }
    return null
}
pn ordinary_scope(source,backup) any^ {
    open tx = (sqlite.existing(source)^) {
        sqlite.preimage(tx,backup) ^ { null }
    }
    return null
}
pn repeat_backup(source,dest,first,second) any^ {
    open tx = (sqlite.clone(source,dest)^) {
        sqlite.preimage(tx,first)^
        sqlite.preimage(tx,second) ^ { null }
    }
    return null
}
pn main() {
    let suffix=uuid()^
    let source="temp/preimage-source-" ++ suffix ++ ".db"
    let backup="temp/preimage-backup-" ++ suffix ++ ".db"
    let upgraded="temp/preimage-upgraded-" ++ suffix ++ ".db"
    let absent="temp/preimage-absent-" ++ suffix ++ ".db"
    let late="temp/preimage-late-" ++ suffix ++ ".db"
    let preserved="temp/preimage-preserved-" ++ suffix ++ ".db"
    let first="temp/preimage-first-" ++ suffix ++ ".db"
    let ready: any^ = create_source(source)
    if (ready is error) { print("unsupported"); return null }
    upgrade(source,upgraded,backup)^
    print((sqlite.query(source,"SELECT SUM(n) FROM sample")^)[0][0]==42 and (sqlite.query(backup,"SELECT SUM(n) FROM sample")^)[0][0]==42 and (sqlite.query(upgraded,"SELECT SUM(n) FROM sample")^)[0][0]==85); print("\n")
    var existing=false
    existing_backup(source,absent,backup) ^ { existing=true }
    print(existing and not exists(absent) and (sqlite.query(backup,"SELECT SUM(n) FROM sample")^)[0][0]==42); print("\n")
    var after_write=false
    late_backup(source,absent,late) ^ { after_write=true }
    print(after_write and not exists(absent) and not exists(late)); print("\n")
    var aborted=false
    abort_after_backup(source,absent,preserved) ^ { aborted=true }
    print(aborted and not exists(absent) and (sqlite.query(preserved,"SELECT SUM(n) FROM sample")^)[0][0]==42); print("\n")
    var ordinary=false
    ordinary_scope(source,late) ^ { ordinary=true }
    print(ordinary and not exists(late) and (sqlite.query(source,"SELECT SUM(n) FROM sample")^)[0][0]==42); print("\n")
    var repeated=false
    repeat_backup(source,absent,first,late) ^ { repeated=true }
    print(repeated and not exists(absent) and not exists(late) and (sqlite.query(first,"SELECT SUM(n) FROM sample")^)[0][0]==42); print("\n")
    let child=start(upgrade,[source,absent,late])
    cancel(child)
    let cancelled: any^ = wait(child)
    print(cancelled is error and not exists(absent) and not exists(late))
    io.delete(source)^; io.delete(backup)^; io.delete(upgraded)^; io.delete(preserved)^; io.delete(first)^
}
