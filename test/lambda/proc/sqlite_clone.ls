// A validated snapshot is published only on clean scope exit (S12.4.4v4).
import sqlite: lambda.io.sqlite

pn initial(path) any^ {
    open tx = (sqlite.create(path)^) {
        sqlite.execute(tx,"CREATE TABLE value (n INTEGER)")^
        sqlite.execute(tx,"INSERT INTO value VALUES (42)")^
    }
    return true
}
pn clone_edit(source,destination) any^ {
    open tx = (sqlite.clone(source,destination)^) {
        let rows=sqlite.query(tx,"SELECT n FROM value")^
        if (rows[0][0]!=42) { raise error("wrong snapshot") }
        sqlite.execute(tx,"INSERT INTO value VALUES (43)")^
    }
    return null
}
pn rejected(source,destination) any^ {
    open tx = (sqlite.clone(source,destination)^) {
        sqlite.execute(tx,"INSERT INTO value VALUES (99)")^
        raise error("reject")
    }
    return null
}
pn main() {
    let suffix=uuid()^
    let source="temp/sqlite-clone-source-" ++ suffix ++ ".db"
    let destination="temp/sqlite-clone-target-" ++ suffix ++ ".db"
    let absent="temp/sqlite-clone-absent-" ++ suffix ++ ".db"
    let missing="temp/sqlite-clone-missing-" ++ suffix ++ ".db"
    let cancelled="temp/sqlite-clone-cancel-" ++ suffix ++ ".db"
    let ready: any^ = initial(source)
    if (ready is error) { print("unsupported"); return null }
    clone_edit(source,destination)^
    let original=sqlite.query(source,"SELECT COUNT(*) FROM value")^
    let copied=sqlite.query(destination,"SELECT COUNT(*) FROM value")^
    print(original[0][0]==1 and copied[0][0]==2); print("\n")
    var existing=false
    clone_edit(source,destination) ^ { existing=true }
    print(existing and (sqlite.query(destination,"SELECT COUNT(*) FROM value")^)[0][0]==2); print("\n")
    var rolled_back=false
    rejected(source,absent) ^ { rolled_back=true }
    print(rolled_back and not exists(absent)); print("\n")
    var missing_error=false
    clone_edit(missing,absent) ^ { missing_error=true }
    print(missing_error and not exists(missing) and not exists(absent)); print("\n")
    let child=start(clone_edit,[source,cancelled])
    cancel(child)
    let cancelled_result: any^ = wait(child)
    print(cancelled_result is error and not exists(cancelled)); print("\n")
    var memory_error=false, reserved_error=false
    sqlite.clone(":memory:",absent) ^ { memory_error=true }
    sqlite.clone("temp/.lambda-stage-protected",absent) ^ { reserved_error=true }
    print(memory_error and reserved_error)
    io.delete(source)^
    io.delete(destination)^
}
