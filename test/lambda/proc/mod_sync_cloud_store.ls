// Transactional fixture for the provider-neutral callback contract; no S3 emulation claim.
import sqlite: lambda.io.sqlite
pub pn create(path) any^ {
    open tx = (sqlite.create(path)^) { sqlite.execute(tx,"CREATE TABLE objects (key TEXT PRIMARY KEY, body TEXT, etag TEXT)")^ }
    return adapter(path)
}
pn read(path, key) any^ {
    open tx = (sqlite.existing(path)^) {
        let rows = sqlite.query(tx,"SELECT body, etag FROM objects WHERE key = ?",[key])^
        return if (len(rows) == 0) null else {body:rows[0][0],etag:rows[0][1]}
    }
}
pn write(path, key, bytes, expected) any^ {
    open tx = (sqlite.existing(path)^) {
        let rows = sqlite.query(tx,"SELECT etag FROM objects WHERE key = ?",[key])^
        if ((len(rows) == 0 and expected != null) or (len(rows) > 0 and rows[0][0] != expected)) {
            return {written:false}
        }
        let token = uuid()^
        sqlite.execute(tx,"INSERT OR REPLACE INTO objects VALUES (?, ?, ?)",[key,bytes,token])^
        return {written:true,etag:token}
    }
}
pn erase(path, key) any^ {
    open tx = (sqlite.existing(path)^) { sqlite.execute(tx,"DELETE FROM objects WHERE key = ?",[key])^ }
    return true
}
pub fn adapter(path) => {erase:pn (key)=> { return erase(path,key)^ },get:pn (key)=> { return read(path,key)^ },head:pn (key)=> { return read(path,key)^ },
    write:pn (key,bytes,expected)=> { return write(path,key,bytes,expected)^ },
    capability:(method,key,headers)=> {url:"https://store.invalid/" ++ key,headers:headers}}
