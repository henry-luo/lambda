import local: lambda.sync.local
import repo: lambda.sync.repository
import chunks: lambda.sync.chunks
import crypto: lambda.io.crypto
import disk: lambda.io.disk
pn main() {
    let area = "temp/sync-pack-large-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/tree")^; io.mkdir(area ++ "/restore")^
    var bytes = binary("0123456789abcdef")^
    while (len(bytes)^ < 8388608) { bytes = bytes ++ bytes }
    disk.install(area ++ "/tree","large.bin",bytes,null)^
    let config = {*:chunks.default_profile,minimum:8388608,target:8388608,maximum:8388608}
    let handle = local.create(area ++ "/repo",crypto.key()^,"large-pack",area ++ "/tree","a",config)^
    local.scan(handle)^
    let checks = repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let saved = repo.load(tx,k)^.state.disk[0].content
        let manifest = repo.content_manifest(tx,k,saved)^
        repo.install_content(tx,k,saved,area ++ "/restore","copy.bin")^
        let suffix = disk.read_stream(area ++ "/restore","copy.bin",{offset:8388592,length:16,max_bytes:16,
            state:null},pn (acc,part)=> { return acc ++ part })^
        let invalid = disk.read_stream(area ++ "/restore","copy.bin",{offset:8388600,length:16},pn (acc,part)=> { return null }) ^ { ^ }
        return [len(manifest.chunks)^ == 1,manifest.chunks[0].size == 8388608,
            repo.availability(tx,k,saved,[],false)^.complete,
            suffix.state == binary("0123456789abcdef")^,invalid is error]
    })^
    let verified = local.verify(handle)^
    io.delete(area)^
    return [*checks,verified.verified]
}
