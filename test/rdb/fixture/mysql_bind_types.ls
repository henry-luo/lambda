// Verify that the MySQL driver retains primitive parameter types on the wire.
import rdb: lambda.io.rdb

pn main() {
    let uri = sys.proc.self.env.LAMBDA_TEST_MYSQL_URI
    if (not (uri is string) or uri == "") { raise error("A test MySQL URI is required") }
    open session=(rdb.database(uri,"mysql")^) {
        let sql = "SELECT '01'=?, '01'=?, '01'=?, '01'=?, '01'=?, '01'=?, '-01'=?, '00'=?, '1.50'=?, '测试'=?, '02147483648'=?"
        rdb.execute(session,sql,[1,1.0,true,"1",null,"01",-1,false,1.5,"测试",2147483648])^
        let result = rdb.fetch(session,2)^
        if (result[1] != [[1,1,1,0,null,1,1,1,1,1,1]] or not result[2]) { raise error("MySQL parameter types differ") }
        rdb.finish(session)^
        rdb.execute(session,"SELECT '01'=?",["1"])^
        let next = rdb.fetch(session,2)^
        if (next[1] != [[0]] or not next[2]) { raise error("MySQL connection was not reusable") }
        rdb.finish(session)^
    }
    return true
}
