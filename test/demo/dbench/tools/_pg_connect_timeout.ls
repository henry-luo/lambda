// S12.4.2: acquisition propagates a driver connection failure to the owning task.
import rdb: lambda.io.rdb

pn connect(uri) any^ {
    open session=(rdb.database(uri,"postgresql")^) { rdb.schema(session)^ }
    return null
}

pn main() {
    let result: any^=connect(sys.proc.self.env.DBENCH_TEST_URI#)
    print(result is error)
}
