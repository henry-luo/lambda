// S12.4.2/S12.4.3: an export is owned and cleaned up by its RDB session task.
import rdb: lambda.io.rdb
import sqlite: lambda.io.sqlite

pn show(value) { print(value); print("\n") }
pn misuse(handle,path) { let result: any^=rdb.export_begin(handle,path); return result is error }

pn main() {
    let base="temp/rdb-export-" ++ (uuid()^)
    let db=base ++ ".db"
    let output=base ++ ".txt"
    let cancelled=base ++ ".cancelled"
    let conflict=base ++ ".conflict"
    let abandoned=base ++ ".abandoned"
    sqlite.execute(db,"CREATE TABLE item (n INTEGER)")^
    open session=(rdb.database(db,"sqlite")^) {
        rdb.execute(session,"SELECT n FROM item")^
        let child=start(misuse,[session,output])^
        show(wait(child)^)
        let bad_path: any^=rdb.export_begin(session,error("path sentinel"))
        show(bad_path is error and bad_path.message=="path sentinel")^
        rdb.export_begin(session,output)^
        let bad_chunk: any^=rdb.export_write(session,error("chunk sentinel"))
        show(bad_chunk is error and bad_chunk.message=="chunk sentinel")^
        rdb.export_write(session,"first")^
        sleep(1)^
        rdb.export_write(session," second")^
        show(not exists(output))
        rdb.export_commit(session)^
        show((input(output,'text')^)=="first second")^
        let duplicate: any^=rdb.export_begin(session,output)
        show(duplicate is error)
        rdb.export_begin(session,cancelled)^
        rdb.export_write(session,"partial")^
        rdb.export_abort(session)^
        show(not exists(cancelled))
        rdb.export_begin(session,conflict)^
        rdb.export_write(session,"staged")^
        io.create_file(conflict,"other")^
        let collision: any^=rdb.export_commit(session)
        show(collision is error and (input(conflict,'text')^)=="other")^
        rdb.export_begin(session,abandoned)^
        rdb.export_write(session,"partial")^
    }
    show(not exists(abandoned))
    io.delete(db)^
    io.delete(output)^
    io.delete(conflict)^
}
