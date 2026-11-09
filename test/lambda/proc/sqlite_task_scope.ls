// S12.4.4v4: an open block in another task does not capture this statement.
import sqlite: lambda.io.sqlite
pn hold_document(parent) any^ {
    open document = temp.'sqlite_other_task' {
        put document.n = 2
        send(parent, "ready")^
        receive()^
    }
    return null
}
pn main() {
    let db = "temp/sqlite-other-task-fixture.db"
    sqlite.execute(db, "DROP TABLE IF EXISTS task_rows")^
    sqlite.execute(db, "CREATE TABLE task_rows (n INTEGER)")^
    let document = temp('sqlite_other_task', {n:1})^
    let child = start(hold_document, [self()])
    receive()^
    sqlite.execute(db, "INSERT INTO task_rows VALUES (7)")^
    send(child, "done")^
    wait(child)^
    let rows = sqlite.query(db, "SELECT n FROM task_rows")^
    print(rows[0][0] == 7 and temp.'sqlite_other_task'#n == 2)
    sqlite.execute(db, "DROP TABLE task_rows")^
}
