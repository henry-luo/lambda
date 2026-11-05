// S12.4.2/S12.4.3: native RDB handles belong to one task and one open scope.
import rdb: lambda.io.rdb
import sqlite: lambda.io.sqlite

pn use_elsewhere(handle) {
    var rejected=false
    rdb.schema(handle) ^ { rejected=true }
    return rejected
}
pn open_missing(path) any^ {
    open session = (rdb.database(path,"sqlite")^) { rdb.schema(session)^ }
    return null
}

pn main() {
    let path="temp/rdb-session-" ++ (uuid()^) ++ ".db"
    sqlite.execute(path,"CREATE TABLE items (a INTEGER NOT NULL, b TEXT NOT NULL, note TEXT, PRIMARY KEY(a,b))")^
    sqlite.execute(path,"CREATE INDEX item_note ON items(note)")^
    sqlite.execute(path,"CREATE TABLE child (a INTEGER, b TEXT, FOREIGN KEY(a,b) REFERENCES items(a,b))")^
    sqlite.execute(path,"CREATE VIEW item_names AS SELECT b FROM items")^
    sqlite.execute(path,"INSERT INTO items VALUES (1,'alpha',NULL),(1,'beta',''),(2,'gamma','last')")^
    var escaped=null
    open session = (rdb.database(path,"sqlite")^) {
        let schema=rdb.schema(session)^
        let items=[for (row in schema where row[0]=="items") row]
        let views=[for (row in schema where row[0]=="item_names") row]
        print(len(items)==3 and items[0][2]=="a" and items[1][2]=="b" and items[2][2]=="note"); print("\n")
        print(items[0][5]==1 and items[1][5]==2 and items[2][5]==0); print("\n")
        print(len(views)==1 and views[0][1] and views[0][2]=="b"); print("\n")
        let details=rdb.details(session)^
        print(["items","index","item_note","note",false] in details and
            ["child","foreign_key","a","items","a"] in details and
            ["child","foreign_key","b","items","b"] in details); print("\n")
        rdb.execute(session,"SELECT a AS n, b AS n, note FROM items WHERE a >= ?1 ORDER BY a,b",[1])^
        let first=rdb.fetch(session,2)^
        print(first[0][0]==["n","integer"] and first[0][1][0]=="n" and len(first[0])==3); print("\n")
        print(first[1]==[[1,"alpha",null],[1,"beta",""]] and not first[2]); print("\n")
        let second=rdb.fetch(session,2)^
        print(second[1]==[[2,"gamma","last"]] and second[2]); print("\n")
        rdb.finish(session)^
        rdb.execute(session,"SELECT b AS n, b AS n FROM items WHERE a=99")^
        let empty=rdb.fetch(session,2)^
        print(len(empty[0])==2 and empty[0][0][0]=="n" and empty[0][1][0]=="n" and len(empty[1])==0 and empty[2]); print("\n")
        rdb.finish(session)^
        rdb.execute(session,"SELECT X'00FF', X'', NULL")^
        let binaries=rdb.fetch(session,1)^
        print(binaries[1]==[[b'\x00FF',binary(""),null]]); print("\n")
        rdb.finish(session)^
        rdb.execute(session,"SELECT a FROM items ORDER BY a")^
        let partial=rdb.fetch(session,1)^
        var busy=false
        rdb.schema(session) ^ {busy=true}
        print(partial[1]==[[1]] and not partial[2] and busy); print("\n")
        rdb.finish(session)^
        rdb.execute(session,"SELECT ?1, ?2",["","O'Reilly"])^
        var oversized=false
        rdb.fetch(session,257) ^ {oversized=true}
        let values=rdb.fetch(session,2)^
        print(oversized and values[1]==[["","O'Reilly"]] and values[2]); print("\n")
        rdb.finish(session)^
        var missing_parameter=false
        rdb.execute(session,"SELECT ?1") ^ {missing_parameter=true}
        var extra_statement=false
        rdb.execute(session,"SELECT 1; SELECT 2") ^ {extra_statement=true}
        var invalid_binary=false
        rdb.execute(session,"SELECT ?1",[b'\x00']) ^ {invalid_binary=true}
        print(missing_parameter and extra_statement and invalid_binary); print("\n")
        let attached=path ++ ".attached"
        var attach_rejected=false
        rdb.execute(session,"ATTACH DATABASE '" ++ attached ++ "' AS other") ^ {attach_rejected=true}
        var write_rejected=false
        rdb.execute(session,"INSERT INTO items VALUES (3,'blocked',NULL)") ^ {write_rejected=true}
        var pragma_rejected=false
        rdb.execute(session,"PRAGMA journal_mode = WAL") ^ {pragma_rejected=true}
        print(attach_rejected and write_rejected and pragma_rejected and not exists(attached)); print("\n")
        let child=start(use_elsewhere,[session])^
        print(wait(child)^); print("\n")
        escaped=session
    }
    var stale=false
    rdb.schema(escaped) ^ { stale=true }
    print(stale); print("\n")
    let missing=path ++ ".missing"
    var refused=false
    open_missing(missing) ^ { refused=true }
    print(refused and not exists(missing))
    io.delete(path)^
}
