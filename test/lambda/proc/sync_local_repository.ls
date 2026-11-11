import sqlite: lambda.io.sqlite
import crypto: lambda.io.crypto
import repo: lambda.sync.repository
import chunks: lambda.sync.chunks

pn main() {
    let db = "temp/sync-local-repository.sqlite"
    io.delete(db) ^ { null }
    io.delete(db ++ ".objects") ^ { null }
    let secret = crypto.key()^
    let keys = repo.keys(secret,"repository-test",db)^
    let config = {algorithm:"lambda-gear32-1",minimum:16,target:64,maximum:256}
    let bytes = binary([for (i in 0 to 4095) (i*i+i*17) % 256])^
    var saved = null
    var duplicate = null
    var count = 0
    var reused = false
    open tx = (sqlite.create(db)^) {
        repo.initialize(tx,keys)^
        saved = repo.content(tx,keys,bytes,config)^
        count = len(repo.inventory(tx)^)
        duplicate = repo.content(tx,keys,bytes,config)^
        reused = len(repo.inventory(tx)^) == count
        repo.commit_head(tx,keys,0,{counter:1,content:saved})^
    }
    let result = repo.access(db,keys,pn (tx,k) => {
        let loaded = repo.load(tx,k)^
        let whole = repo.payload(tx,k,saved)^
        let empty = repo.content(tx,k,binary([])^,config)^
        let old = loaded.state
        repo.commit_head(tx,k,loaded.generation,{counter:2,content:saved,previous:loaded.id})^
        return {restored:whole == bytes,loaded:old.counter == 1,empty:repo.payload(tx,k,empty)^ == binary([])^,
            checked:repo.verify(tx,k)^.verified}
    })^
    let wrong = repo.access(db,repo.keys(crypto.key()^,"repository-test")^,pn (tx,k)=> { return repo.load(tx,k)^ }) ^ { true }
    let stale = repo.access(db,keys,pn (tx,k)=> { return repo.commit_head(tx,k,1,{counter:3})^ }) ^ { true };
    [saved == duplicate,result.restored,result.loaded,result.empty,reused,result.checked,wrong,stale,
     all([for (piece in chunks.split(bytes,config)^) len(piece) <= 256])]
}
