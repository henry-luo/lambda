// S13.3.2: cancellation interrupts a running fetch before session cleanup.
import rdb: lambda.io.rdb
import sqlite: lambda.io.sqlite

pn slow(path, owner) any^ {
    open session = (rdb.database(path,"sqlite")^) {
        rdb.execute(session,"WITH RECURSIVE seq(n) AS (VALUES(1) UNION ALL SELECT n+1 FROM seq WHERE n<100000000) SELECT SUM(n) FROM seq")^
        send(owner,"ready")^
        rdb.fetch(session,1)^
    }
    return null
}
pn main() {
    let path="temp/rdb-cancel-" ++ (uuid()^) ++ ".db"
    sqlite.execute(path,"CREATE TABLE test_value (n INTEGER)")^
    let task=start(slow,[path,self()])
    receive()^
    sleep(30)^
    cancel(task)
    let outcome: any^=wait(task)
    print(outcome is error and outcome.code==319); print("\n")
    open session = (rdb.database(path,"sqlite")^) {
        rdb.execute(session,"SELECT 42 AS n")^
        let batch=rdb.fetch(session,2)^
        print(batch[1]==[[42]] and batch[2])
        rdb.finish(session)^
    }
    io.delete(path)^
}
