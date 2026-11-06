// RDB5: composite cursor predicates bind values across each network driver.
import rdb: lambda.io.rdb
import sql: ~~.mod_sql

pn main() {
    let driver=sys.proc.self.env.DBENCH_TEST_DRIVER#
    let uri=sys.proc.self.env.DBENCH_TEST_URI#
    let table=sys.proc.self.env.DBENCH_TEST_TABLE#
    let page=sql.table_page(driver,table,["a","b","marker"],
        [{column:"marker",op:"=",value:"wanted"}],[],["a","b"],1,2,null,[1,"a"])^
    open session = (rdb.database(uri,driver)^) {
        rdb.execute(session,page.sql,page.params)^
        let batch=rdb.fetch(session,3)^
        print(batch[1]==[[1,"b","wanted"],[2,"a","wanted"]] and batch[2]); print("\n")
        rdb.finish(session)^
        print(page.keyset and page.params==["wanted",1,1,"a",2]); print("\n")
        let descending=sql.table_page(driver,table,["a","b","marker"],
            [{column:"marker",op:"=",value:"wanted"}],[{column:"b",direction:"DESC"}],["a","b"],1,2,null,["b",1])^
        rdb.execute(session,descending.sql,descending.params)^
        let ordered=rdb.fetch(session,3)^
        print(ordered[1]==[[1,"a","wanted"],[2,"a","wanted"]] and ordered[2])
        rdb.finish(session)^
    }
}
