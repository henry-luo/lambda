import rdb: lambda.io.rdb
pn slow(uri,driver,owner) any^ {
    open session = (rdb.database(uri,driver)^) {
        let sql=if (driver=="mysql") "SELECT SLEEP(30)" else "SELECT pg_sleep(30)"
        rdb.execute(session,sql)^
        send(owner,"ready")^
        rdb.fetch(session,1)^
    }
    return null
}
pn main() {
    let driver=sys.proc.self.env.DBENCH_TEST_DRIVER#
    let uri=sys.proc.self.env.DBENCH_TEST_URI#
    let task=start(slow,[uri,driver,self()])
    receive()^
    sleep(500)^
    cancel(task)
    let outcome: any^=wait(task)
    print(outcome is error and outcome.code==319); print("\n")
    open session = (rdb.database(uri,driver)^) {
        rdb.execute(session,"SELECT 42 AS n")^
        let batch=rdb.fetch(session,2)^
        print(batch[1]==[[42]] and batch[2])
        rdb.finish(session)^
    }
}
