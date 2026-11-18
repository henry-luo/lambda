import local: lambda.sync.local
import repo: lambda.sync.repository
import fs: lambda.sync.filesystem
import chunks: lambda.sync.chunks
import crypto: lambda.io.crypto
import disk: lambda.io.disk
pn main() {
    let area = "temp/sync-capture-boundary-" ++ string(sys.proc.self.pid)
    io.mkdir(area ++ "/tree")^
    let config = {*:chunks.default_profile,minimum:16,target:32,maximum:64}
    let handle = local.create(area ++ "/repo.sqlite",crypto.key()^,"boundary",area ++ "/tree","device",config)^
    let small = binary([for (i in 0 to 4095) i%251])^
    let large = small ++ b'01'
    disk.install(area,"small",small,null)^; disk.install(area,"large",large,null)^
    let checks = repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let a = fs.capture_file(tx,k,area,"small",config,8192)^
        let b = fs.capture_file(tx,k,area,"large",config,8192)^
        let inline = repo.get(tx,k,a.content.manifest,"manifest")^
        let streamed = repo.get(tx,k,b.content.manifest,"manifest")^
        let verified = repo.availability(tx,k,b.content,[],false)^
        return [repo.legacy_capture_limit(config) == 4096,inline.version == 1,streamed.version == 2,
            repo.payload(tx,k,a.content)^ == small,repo.payload(tx,k,b.content)^ == large,
            verified.complete,verified.bytes == null]
    })^
    io.delete(area)^
    return checks
}
