import local: lambda.sync.local
import crypto: lambda.io.crypto
import disk: lambda.io.disk
import h: ~~.mod_sync_local_helpers

pn main() {
    let area = "temp/sync-local-binary"
    io.delete(area) ^ { null }
    io.mkdir(area ++ "/a/empty")^
    io.mkdir(area ++ "/b")^
    io.mkdir(area ++ "/restore")^
    output(binary([0,255,1])^,area ++ "/a/opaque.bin")^
    io.create_file(area ++ "/a/zero.bin","")^
    let secret = crypto.key()^
    let a = local.create(area ++ "/a.sqlite",secret,"binary-files",area ++ "/a","Alice")^
    let b = local.create(area ++ "/b.sqlite",secret,"binary-files",area ++ "/b","Bob")^
    local.scan(a)^
    let pinned = local.snapshot(a,"binary capture")^.state.snapshots[0]
    h.transfer(a,b)^
    let installed = local.materialize(b)^
    let copied = disk.capture(area ++ "/b","zero.bin",0)^
    let empty = disk.entries(area ++ "/b","empty",10)^
    let restored = local.restore(a,pinned,area ++ "/restore")^
    let verified = local.verify(b)^;
    [installed.state.installation == null,copied.bytes == null,copied.size == 0,len(empty) == 0,
     input(area ++ "/b/opaque.bin",'binary')^ == binary([0,255,1])^,
     input(area ++ "/restore/opaque.bin",'binary')^ == binary([0,255,1])^,
     disk.capture(area ++ "/restore","zero.bin",0)^.bytes == null,
     len(disk.entries(area ++ "/restore","empty",10)^) == 0,restored.entries == 3,verified.verified]
}
