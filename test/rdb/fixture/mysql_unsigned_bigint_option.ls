// RDB6: preserve unsigned BIGINT values beyond the signed integer range.
import rdb: lambda.io.rdb

pn check(uri, sql, expected_type, expected_value) any^ {
    open session=(rdb.database(uri,"mysql")^) {
        rdb.execute(session,sql,[])^
        let result = rdb.fetch(session,2)^
        if (result[0] != [["v",expected_type]] or result[1] != [[expected_value]] or not result[2]) {
            raise error("MySQL unsigned BIGINT option differs")
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
    let prefix = uri ++ (if (contains(uri,"?")^) "&" else "?") ++ "unsigned-bigint-as-decimal="
    check(prefix ++ "false","SELECT CAST('1' AS UNSIGNED) AS v","integer",1)^
    check(prefix ++ "true","SELECT CAST('18446744073709551615' AS UNSIGNED) AS v","decimal","18446744073709551615")^
    var rejected = false
    open_invalid(prefix ++ "invalid") ^ { rejected = true }
    if (not rejected) { raise error("Invalid unsigned BIGINT option was accepted") }
    return true
}
