// RDB6: the lossless option preserves the physical TINYINT(1) values.
import rdb: lambda.io.rdb

pn check(uri, expected_type, expected_rows) any^ {
    open session=(rdb.database(uri,"mysql")^) {
        rdb.execute(session,"SELECT active FROM authors ORDER BY id",[])^
        let result = rdb.fetch(session,10)^
        if (result[0] != [["active",expected_type]] or result[1] != expected_rows or not result[2]) {
            raise error("MySQL TINYINT option differs")
        }
        rdb.finish(session)^
    }
    return null
}

pn open_invalid(uri) any^ {
    open session=(rdb.database(uri,"mysql")^) { rdb.schema(session)^ }
    return null
}

pn main() {
    let uri = sys.proc.self.env.LAMBDA_TEST_MYSQL_URI
    if (not (uri is string) or uri == "") { raise error("A test MySQL URI is required") }
    let prefix = uri ++ (if (contains(uri,"?")^) "&" else "?") ++ "tinyint-as-integer="
    check(prefix ++ "false","boolean",[[true],[true],[false]])^
    check(prefix ++ "true","integer",[[1],[1],[0]])^
    var rejected = false
    open_invalid(prefix ++ "invalid") ^ { rejected = true }
    if (not rejected) { raise error("Invalid TINYINT option was accepted") }
    return true
}
