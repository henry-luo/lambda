import local: lambda.sync.local
import repo: lambda.sync.repository
import fs: lambda.sync.filesystem
import crypto: lambda.io.crypto
import disk: lambda.io.disk
pn main() {
    let area = "temp/sync-capture-errors-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/tree")^
    output("first",area ++ "/tree/file")^
    let h = local.create(area ++ "/repo.sqlite",crypto.key()^,"capture-errors",area ++ "/tree","a")^
    let before = local.scan(h)^
    let token = disk.inspect(area ++ "/tree","file")^.token
    output("external edit",area ++ "/tree/file")^
    let stale = repo.access(h.path,h.keys,pn (tx,k)=> {
        return fs.capture_file(tx,k,area ++ "/tree","file",before.state.chunker,1024,token)^
    }) ^ { ^ }
    let missing = repo.access(h.path,h.keys,pn (tx,k)=> {
        return fs.capture_file(tx,k,area ++ "/tree","removed",before.state.chunker,1024)^
    }) ^ { ^ }
    let oversized = repo.access(h.path,h.keys,pn (tx,k)=> {
        return fs.capture_file(tx,k,area ++ "/tree","file",before.state.chunker,1)^
    }) ^ { ^ }
    let budget = repo.access(h.path,h.keys,pn (tx,k)=> {
        return fs.capture(tx,k,before.state,false,0)^
    }) ^ { ^ }
    let replaced = repo.access(h.path,h.keys,pn (tx,k)=> {
        return fs.capture(tx,k,{*:before.state,root_identity:b'00'})^
    }) ^ { ^ }
    let held = local.status(h)^
    let after = local.scan(h)^
    let result = stale is error and stale.code == fs.capture_changed_code and
        missing is error and missing.code == fs.capture_changed_code and
        oversized is error and oversized.code != fs.capture_changed_code and
        budget is error and budget.code != fs.capture_changed_code and
        replaced is error and replaced.code != fs.capture_changed_code and
        held.generation == before.generation and after.generation == before.generation+1 and
        input(area ++ "/tree/file",'text')^ == "external edit"
    io.delete(area)^
    return result
}
