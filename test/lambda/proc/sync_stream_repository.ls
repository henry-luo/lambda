import local: lambda.sync.local
import repo: lambda.sync.repository
import chunks: lambda.sync.chunks
import crypto: lambda.io.crypto
import disk: lambda.io.disk
import reach: lambda.sync.reachability

pn main() {
    let area = "temp/sync-stream-repository-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/tree")^
    io.mkdir(area ++ "/export")^
    let config = {*:chunks.default_profile,minimum:16,target:32,maximum:64}
    let agent = local.create(area ++ "/repo.sqlite",crypto.key()^,"stream-repository",area ++ "/tree","stream",config)^
    let bytes = binary([for (i in 0 to 16383) i%251])^
    disk.install(area,"source",bytes,null)^
    let result = repo.access(agent.path,agent.keys,pn (tx,k)=> {
        let captured = repo.content_file(tx,k,area,"source",config)^
        let ordinary = repo.content(tx,k,bytes,config)^
        let manifest = repo.get(tx,k,captured.content.manifest,"manifest")^
        let expanded = repo.content_manifest(tx,k,captured.content)^
        repo.install_content(tx,k,captured.content,area ++ "/export","copy")^
        let closure = reach.closure(tx,k,[captured.content.manifest],{revisions:[],versions:[]})^
        let chunk = expanded.chunks[0].id
        let record = repo.record(k,chunk)^
        repo.remove_object(tx,k,chunk)^
        let deferred = repo.availability(tx,k,captured.content,[chunk])^
        repo.import_objects(tx,k,[record])^
        return [record.kind == "chunk_raw",manifest.version == 2,len(manifest.pages)^ > 1,captured.content.hash == ordinary.hash,
            repo.payload(tx,k,captured.content)^ == bytes,
            all([for (page in manifest.pages) contains(closure,page.id)^]),
            not deferred.complete,len(deferred.missing)^ > 0,repo.availability(tx,k,captured.content)^.complete]
    })^
    let copy = disk.capture(area ++ "/export","copy",32768)^
    io.delete(area)^
    return [*result,copy.bytes == bytes]
}
