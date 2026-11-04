// SQLite must authorize exactly the path it will open (D11.1.2).
import sqlite: lambda.io.sqlite

pn main() {
    let longest=join([for (i in 1 to 1023) "x"],"")^
    let oversized=longest ++ "x"
    let admitted: any^ = sqlite.database(longest)
    var target_rejected=false, statement_rejected=false
    var uri_target_rejected=false, uri_statement_rejected=false
    sqlite.database(oversized) ^ { target_rejected=true }
    sqlite.execute(oversized,"SELECT 1") ^ { statement_rejected=true }
    sqlite.database("file::memory:?mode=memory") ^ { uri_target_rejected=true }
    sqlite.query("file::memory:?mode=memory","SELECT 1") ^ { uri_statement_rejected=true }
    print([not (admitted is error),target_rejected,statement_rejected,uri_target_rejected,uri_statement_rejected])
}
