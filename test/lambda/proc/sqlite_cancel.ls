// S12.4.4v4: cancellation of a running statement rolls back before wait returns.
import sqlite: lambda.io.sqlite
pn slow_insert(db) any^ {
    sqlite.execute(db, "WITH RECURSIVE seq(n) AS (VALUES(1) UNION ALL SELECT n + 1 FROM seq WHERE n < 100000000) INSERT INTO cancellation_rows SELECT n FROM seq")^
    return null
}
pn main() {
    let db = "temp/sqlite-cancellation-fixture.db"
    sqlite.execute(db, "DROP TABLE IF EXISTS cancellation_rows")^
    sqlite.execute(db, "CREATE TABLE cancellation_rows (n INTEGER)")^
    let task = start(slow_insert, [db])
    sleep(30)^
    cancel(task)
    let result: any^ = wait(task)
    print(result is error and result.code == 319); print("\n")
    let rows = sqlite.query(db, "SELECT COUNT(*) FROM cancellation_rows")^
    print(rows[0][0] == 0); print("\n")
    sqlite.execute(db, "INSERT INTO cancellation_rows VALUES (7)")^
    let after = sqlite.query(db, "SELECT n FROM cancellation_rows")^
    print(after[0][0] == 7)
    sqlite.execute(db, "DROP TABLE cancellation_rows")^
}
