// S12.4.4v4: scoped SQL reads its own writes and commits or rolls back as one unit.
import sqlite: lambda.io.sqlite
pn returned(db) int^ {
    open tx = (sqlite.database(db)^) {
        sqlite.execute(tx, "INSERT INTO entries VALUES (10, 'return')")^
        return 12
    }
    return 0
}
pn wrong_return(db, value) int^ {
    open tx = (sqlite.database(db)^) {
        sqlite.execute(tx, "INSERT INTO entries VALUES (11, 'bad return')")^
        return value
    }
    return 0
}
pn poisoned(db) any^ {
    open tx = (sqlite.database(db)^) {
        sqlite.execute(tx, "INSERT INTO entries VALUES (20, 'pending')")^
        sqlite.execute(tx, "INSERT INTO entries VALUES (21, NULL)") ^ { null }
        let visible = sqlite.query(tx, "SELECT COUNT(*) FROM entries WHERE id = 20")^
        if (visible[0][0] != 1) { raise error("read-your-writes failed") }
    }
    return null
}
pn bad_parameter(db) any^ {
    open tx = (sqlite.database(db)^) {
        sqlite.execute(tx, "INSERT INTO entries VALUES (30, 'pending')")^
        sqlite.execute(tx, "INSERT INTO entries VALUES (?1, 'bad')", [{}]) ^ { null }
    }
    return null
}
pn raised(db) any^ {
    open tx = (sqlite.database(db)^) {
        sqlite.execute(tx, "INSERT INTO entries VALUES (40, 'pending')")^
        raise error("stop")
    }
    return null
}
pn waiting_transaction(db, parent) any^ {
    open tx = (sqlite.database(db)^) {
        sqlite.execute(tx, "INSERT INTO entries VALUES (50, 'pending')")^
        send(parent, "ready")^
        sleep(10000)^
    }
    return null
}
pn busy_transaction(db, parent) any^ {
    open tx = (sqlite.database(db)^) {
        sqlite.execute(tx, "INSERT INTO entries VALUES (70, 'pending')")^
        send(parent, "ready")^
        sqlite.execute(tx, "WITH RECURSIVE seq(n) AS (VALUES(1) UNION ALL SELECT n + 1 FROM seq WHERE n < 100000000) INSERT INTO heavy SELECT n FROM seq")^
    }
    return null
}
pn misuse_handle(tx) {
    var rejected = false
    sqlite.query(tx, "SELECT 1") ^ { rejected = true }
    return rejected
}
pn nested_transaction(db) any^ {
    open outer = (sqlite.database(db)^) {
        sqlite.execute(outer, "INSERT INTO entries VALUES (90, 'pending')")^
        open inner = (sqlite.database(db)^) { sqlite.query(inner, "SELECT 1")^ }
    }
    return null
}
pn holding_transaction(db, parent) any^ {
    open tx = (sqlite.database(db)^) {
        sqlite.execute(tx, "INSERT INTO entries VALUES (60, 'pending')")^
        send(parent, "ready")^
        receive()^
    }
    return null
}
pn main() {
    let db = "temp/sqlite-scoped-fixture.db"
    sqlite.execute(db, "DROP TABLE IF EXISTS entries")^
    sqlite.execute(db, "CREATE TABLE entries (id INTEGER PRIMARY KEY, name TEXT NOT NULL)")^
    sqlite.execute(db, "DROP TABLE IF EXISTS heavy")^
    sqlite.execute(db, "CREATE TABLE heavy (n INTEGER)")^
    var escaped = null
    open tx = (sqlite.database(db)^) {
        sqlite.execute(tx, "INSERT INTO entries VALUES (?1, ?2)", [1, "one"])^
        sqlite.execute(tx, "INSERT INTO entries VALUES (?1, ?2)", [2, "two"])^
        let visible = sqlite.query(tx, "SELECT COUNT(*) FROM entries")^
        print(visible[0][0] == 2); print("\n")
        let child = start(misuse_handle, [tx])
        print(wait(child)^); print("\n")
        escaped = tx
    }
    let after = sqlite.query(db, "SELECT COUNT(*) FROM entries")^
    print(after[0][0] == 2); print("\n")
    var stale = false
    sqlite.query(escaped, "SELECT 1") ^ { stale = true }
    print(stale); print("\n")
    print(returned(db)^ == 12); print("\n")
    var type_failed = false
    wrong_return(db, "bad") ^ { type_failed = true }
    print(type_failed); print("\n")
    var poisoned_error = false
    poisoned(db) ^ { poisoned_error = true }
    print(poisoned_error); print("\n")
    var parameter_error = false
    bad_parameter(db) ^ { parameter_error = true }
    print(parameter_error); print("\n")
    var raise_error = false
    raised(db) ^ { raise_error = true }
    print(raise_error); print("\n")
    let check = sqlite.query(db, "SELECT COUNT(*) FROM entries WHERE id IN (10, 11, 20, 30, 40)")^
    print(check[0][0] == 1); print("\n")
    let cancelling = start(waiting_transaction, [db, self()])
    receive()^
    cancel(cancelling)
    let cancelled: any^ = wait(cancelling)
    print(cancelled is error and cancelled.code == 319); print("\n")
    let no_cancelled = sqlite.query(db, "SELECT COUNT(*) FROM entries WHERE id = 50")^
    print(no_cancelled[0][0] == 0); print("\n")
    let busy = start(busy_transaction, [db, self()])
    receive()^
    sleep(30)^
    cancel(busy)
    let busy_result: any^ = wait(busy)
    let busy_count = sqlite.query(db, "SELECT COUNT(*) FROM entries WHERE id = 70")^
    let heavy_count = sqlite.query(db, "SELECT COUNT(*) FROM heavy")^
    print(busy_result is error and busy_result.code == 319 and busy_count[0][0] == 0 and heavy_count[0][0] == 0); print("\n")
    var nested = false
    nested_transaction(db) ^ { nested = true }
    let nested_count = sqlite.query(db, "SELECT COUNT(*) FROM entries WHERE id = 90")^
    print(nested and nested_count[0][0] == 0); print("\n")
    while (true) {
        open tx = (sqlite.database(db)^) { sqlite.execute(tx, "INSERT INTO entries VALUES (80, 'break')")^; break }
    }
    var i = 0
    while (i < 2) {
        i = i + 1
        open tx = (sqlite.database(db)^) { sqlite.execute(tx, "INSERT INTO entries VALUES (?1, 'continue')", [80 + i])^; continue }
    }
    let loop_count = sqlite.query(db, "SELECT COUNT(*) FROM entries WHERE id BETWEEN 80 AND 82")^
    print(loop_count[0][0] == 3); print("\n")
    let holding = start(holding_transaction, [db, self()])
    receive()^
    let before_commit = sqlite.query(db, "SELECT COUNT(*) FROM entries WHERE id = 60")^
    print(before_commit[0][0] == 0); print("\n")
    send(holding, "done")^
    wait(holding)^
    let after_commit = sqlite.query(db, "SELECT COUNT(*) FROM entries WHERE id = 60")^
    print(after_commit[0][0] == 1)
    sqlite.execute(db, "DROP TABLE entries")^
    sqlite.execute(db, "DROP TABLE heavy")^
}
