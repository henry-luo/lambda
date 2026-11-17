// Transactional fixture for the provider-neutral callback contract; no S3 emulation claim.
import sqlite: lambda.io.sqlite
pub pn create(path) any^ {
    open tx = (sqlite.create(path)^) { sqlite.execute(tx,"CREATE TABLE objects (key TEXT PRIMARY KEY, body TEXT, etag TEXT)")^ }
    return adapter(path)
}
pn access(source, action) any^ {
    if (source is string) {
        open tx = (sqlite.existing(source)^) { return action(tx)^ }
    }
    return action(source)^
}
// batch algorithm work explicitly; the callback's adapter cannot outlive its SQL scope.
pub pn scoped(path, action) any^ {
    open tx = (sqlite.existing(path)^) { return action(adapter(tx))^ }
}
pn read(source, key) any^ {
    return access(source,pn (tx)=> {
        let rows = sqlite.query(tx,"SELECT body, etag FROM objects WHERE key = ?",[key])^
        return if (len(rows) == 0) null else {body:rows[0][0],etag:rows[0][1]}
    })^
}
pn write(source, key, bytes, expected) any^ {
    return access(source,pn (tx)=> {
        let rows = sqlite.query(tx,"SELECT etag FROM objects WHERE key = ?",[key])^
        if ((len(rows) == 0 and expected != null) or (len(rows) > 0 and rows[0][0] != expected)) {
            return {written:false}
        }
        let token = uuid()^
        sqlite.execute(tx,"INSERT OR REPLACE INTO objects VALUES (?, ?, ?)",[key,bytes,token])^
        return {written:true,etag:token}
    })^
}
pn erase(source, key) any^ {
    return access(source,pn (tx)=> { sqlite.execute(tx,"DELETE FROM objects WHERE key = ?",[key])^; return true })^
}
pn listing(source, prefix, cursor, limit) any^ {
    if (not (limit is int) or limit < 1 or limit > 128) raise error("fixture: invalid listing limit")
    return access(source,pn (tx)=> {
        let rows = sqlite.query(tx,"SELECT key, etag FROM objects WHERE key >= ? ORDER BY key",[prefix])^
        let entries = [for (row in rows where starts_with(row[0],prefix)^ and (cursor == null or row[0] > cursor))
            {key:row[0],etag:row[1],size:0}]
        let selected = take(entries,limit)
        return {entries:selected,next:if (len(entries)^ > limit) selected[len(selected)^-1].key else null}
    })^
}
pub fn adapter(source) => {enumerate:pn (prefix,cursor,limit)=> { return listing(source,prefix,cursor,limit)^ },
    erase:pn (key)=> { return erase(source,key)^ },get:pn (key)=> { return read(source,key)^ },head:pn (key)=> { return read(source,key)^ },
    write:pn (key,bytes,expected)=> { return write(source,key,bytes,expected)^ },
    capability:(method,key,headers)=> {url:"https://store.invalid/" ++ key,headers:headers}}
