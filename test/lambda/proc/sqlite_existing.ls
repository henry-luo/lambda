// Existing-only acquisition must not create a missing database (S12.4.2).
import sqlite: lambda.io.sqlite

pn acquire(path) any^ {
    let target = sqlite.existing(path)^
    open tx = target { sqlite.query(tx, "SELECT 1")^ }
    return true
}

pn main() {
    let path = "temp/sqlite-existing-" ++ (uuid()^) ++ ".db"
    var rejected = false
    acquire(path) ^ { rejected = true }
    print(rejected and not exists(path)); print("\n")
    sqlite.execute(path, "CREATE TABLE value (n INTEGER)")^
    let target = sqlite.existing(path)^
    open tx = target { sqlite.execute(tx, "INSERT INTO value VALUES (42)")^ }
    let rows = sqlite.query(path, "SELECT n FROM value")^
    print(rows[0][0] == 42); print("\n")
    var memory_rejected = false
    acquire(":memory:") ^ { memory_rejected = true }
    var uri_rejected = false
    acquire("file::memory:?mode=memory") ^ { uri_rejected = true }
    print(memory_rejected and uri_rejected)
    io.delete(path)^
}
