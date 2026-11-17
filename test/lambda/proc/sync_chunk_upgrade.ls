import local: lambda.sync.local
import repo: lambda.sync.repository
import chunks: lambda.sync.chunks
import crypto: lambda.io.crypto
pn main() {
    let area = "temp/sync-chunk-upgrade-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/tree")^
    let secret = crypto.key()^
    let handle = local.create(area ++ "/repo",secret,"upgrade",area ++ "/tree","a",chunks.legacy_profile)^
    output("old version",area ++ "/tree/file")^
    let initial = local.scan(handle)^.state
    let reference = initial.disk[0].content
    let reopened = local.open_repository(handle.path,secret,"upgrade")^
    let verified = local.scan(reopened,null,true)^.state
    output("new version",area ++ "/tree/file")^
    let updated = local.scan(reopened)^.state
    let changed = repo.access(reopened.path,reopened.keys,pn (tx,k)=> {
        return repo.get(tx,k,updated.disk[0].content.manifest,"manifest")^.profile.algorithm == chunks.default_profile.algorithm
    })^
    let checks = [verified.chunker == chunks.default_profile,verified.disk[0].content == reference,
        verified.endpoint.replica.clock == initial.endpoint.replica.clock,changed,
        local.read_content(reopened,reference)^ == binary("old version")^,
        local.read_content(reopened,updated.disk[0].content)^ == binary("new version")^]
    io.delete(area)^
    return checks
}
