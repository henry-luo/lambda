// D9.3.4/S12.4.4v4: separate statements run on parked tasks and commit independently.
import sqlite: lambda.io.sqlite
pn reader(db) any^ {
    return sqlite.query(db, "WITH RECURSIVE seq(n) AS (VALUES(1) UNION ALL SELECT n + 1 FROM seq WHERE n < 100000) SELECT SUM(n) FROM seq")^
}
pn main() {
    let db = "temp/sqlite-parallel-fixture.db"
    let a = start(reader, [db])
    let b = start(reader, [db])
    let first = wait(a)^
    let second = wait(b)^
    print(first[0][0] == 5000050000 and second[0][0] == 5000050000)
}
