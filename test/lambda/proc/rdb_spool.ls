// S12.4.2/S12.4.3: the result spool is owned by its session task.
import rdb: lambda.io.rdb
import sqlite: lambda.io.sqlite

pn read_elsewhere(session) {
    var rejected=false
    rdb.spool_read(session,0,1) ^ { rejected=true }
    return rejected
}

pn main() {
    let suffix=uuid()^
    let path="temp/rdb-spool-test-" ++ suffix ++ ".db"
    let directory="temp/rdb-spool-test-" ++ suffix
    sqlite.execute(path,"CREATE TABLE items (id INTEGER, value TEXT, data BLOB)")^
    sqlite.execute(path,"INSERT INTO items VALUES (1,'alpha',X'00FF'),(2,'',X''),(3,NULL,NULL)")^
    open session = (rdb.database(path,"sqlite")^) {
        rdb.execute(session,"SELECT id,value,data FROM items ORDER BY id")^
        var unavailable=false
        rdb.spool_begin(session,"temp/missing-spool-parent-" ++ suffix ++ "/spool") ^ {unavailable=true}
        print(unavailable); print("\n")
        rdb.spool_begin(session,directory)^
        let first=rdb.fetch(session,2)^
        let second=rdb.fetch(session,2)^
        print(first[1]==[[1,"alpha",b'\x00FF'],[2,"",binary("")]] and second[1]==[[3,null,null]]); print("\n")
        print(rdb.spool_read(session,1,2)^==[[2,"",binary("")],[3,null,null]]); print("\n")
        let child=start(read_elsewhere,[session])^
        print(wait(child)^); print("\n")
        rdb.finish(session)^
        print(rdb.spool_read(session,0,1)^==[[1,"alpha",b'\x00FF']]); print("\n")
        var invalid=false
        rdb.spool_read(session,2,2) ^ {invalid=true}
        print(invalid); print("\n")
        rdb.execute(session,"SELECT id FROM items")^
        var old_gone=false
        rdb.spool_read(session,0,1) ^ {old_gone=true}
        let one=rdb.fetch(session,1)^
        var late=false
        rdb.spool_begin(session,directory) ^ {late=true}
        print(old_gone and late and one[1]==[[1]]); print("\n")
        rdb.finish(session)^
        rdb.execute(session,"SELECT 9223372036854775807, -3.25, X'00FF'")^
        rdb.spool_begin(session,directory)^
        let wide=rdb.fetch(session,2)^
        print(rdb.spool_read(session,0,1)^==wide[1]); print("\n")
        rdb.finish(session)^
        rdb.execute(session,"SELECT id FROM items ORDER BY id")^
        rdb.spool_begin(session,directory)^
        rdb.fetch(session,1)^
        rdb.spool_seal(session)^
        let tail=rdb.fetch(session,2)^
        var beyond=false
        rdb.spool_read(session,1,1) ^ {beyond=true}
        print(tail[1]==[[2],[3]] and beyond and rdb.spool_read(session,0,1)^==[[1]])
        rdb.finish(session)^
    }
    io.delete(path)^
    io.delete(directory)^
}
