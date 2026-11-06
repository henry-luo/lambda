import rdb: lambda.io.rdb
pn main() {
    let driver=sys.proc.self.env.DBENCH_TEST_DRIVER#
    let uri=sys.proc.self.env.DBENCH_TEST_URI#
    open session = (rdb.database(uri,driver)^) {
        let schema=rdb.schema(session)^
        print(len(schema)>0); print("\n")
        let details=rdb.details(session)^
        print(len([for (row in details where row[1]=="index") row])>0); print("\n")
        rdb.execute(session,"SELECT 1 AS n, 10 AS n UNION ALL SELECT 2,20 UNION ALL SELECT 3,30")^
        let first=rdb.fetch(session,2)^
        let final_batch=rdb.fetch(session,2)^
        print(first[0][0][0]=="n" and first[0][1][0]=="n" and first[1]==[[1,10],[2,20]] and not first[2]); print("\n")
        print(final_batch[1]==[[3,30]] and final_batch[2]); print("\n")
        rdb.finish(session)^
        let param_sql=if (driver=="mysql") "SELECT CAST(? AS SIGNED) AS answer" else "SELECT $1::bigint AS answer"
        rdb.execute(session,param_sql,[42])^
        let answer=rdb.fetch(session,1)^
        print(answer[1][0][0]==42); print("\n")
        rdb.finish(session)^
        rdb.execute(session,"SELECT 1 AS duplicate, 2 AS duplicate WHERE 1=0")^
        let empty=rdb.fetch(session,2)^
        print(len(empty[0])==2 and empty[0][0][0]=="duplicate" and len(empty[1])==0 and empty[2])
        rdb.finish(session)^
    }
}
