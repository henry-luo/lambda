import local: lambda.sync.local
import crypto: lambda.io.crypto
import h: ~~.mod_sync_local_helpers
import c: lambda.sync.core

pn main() {
    let area = "temp/sync-local-filesystem"
    io.delete(area) ^ { null }
    io.mkdir(area ++ "/a/folder")^
    io.mkdir(area ++ "/b")^
    io.mkdir(area ++ "/restore")^
    output("original",area ++ "/a/folder/a.txt")^
    let secret = crypto.key()^
    let alice = local.create(area ++ "/a.sqlite",secret,"local-files",area ++ "/missing", "Alice") ^ { null }
    let a = local.create(area ++ "/alice.sqlite",secret,"local-files",area ++ "/a","Alice")^
    let b = local.create(area ++ "/bob.sqlite",secret,"local-files",area ++ "/b","Bob")^
    let initial = local.scan(a)^.state
    let original_id = h.at_path(initial,"folder/a.txt")^.id
    let pinned = local.snapshot(a,"before edits")^.state.snapshots[0]
    h.transfer(a,b)^
    let installed = local.materialize(b)^
    let copied = input(area ++ "/b/folder/a.txt",'text')^
    let before = local.scan(b)^.state.endpoint.replica
    let repeated = local.scan(b)^.state.endpoint.replica
    io.rename(area ++ "/a/folder",area ++ "/a/renamed")^
    output("edited",area ++ "/a/renamed/a.txt")^
    output("offline Bob",area ++ "/b/b.txt")^
    let restarted_a = local.open_repository(a.path,secret,"local-files")^
    let restarted_b = local.open_repository(b.path,secret,"local-files")^
    let changed = local.scan(restarted_a)^.state
    local.scan(restarted_b)^
    h.transfer(restarted_a,restarted_b)^
    h.transfer(restarted_b,restarted_a)^
    local.materialize(restarted_a)^
    local.materialize(restarted_b)^
    let final_a = local.status(restarted_a)^.state
    let final_b = local.status(restarted_b)^.state
    let restored = local.restore(restarted_a,pinned,area ++ "/restore")^
    let checked = local.verify(restarted_b)^;
    [alice == null,copied == "original",installed.state.installation == null,
     c.compact(before) == c.compact(repeated),h.at_path(changed,"renamed/a.txt")^.id == original_id,
     c.compact(final_a.endpoint.replica) == c.compact(final_b.endpoint.replica),
     input(area ++ "/a/renamed/a.txt",'text')^ == "edited",
     input(area ++ "/b/b.txt",'text')^ == "offline Bob",
     input(area ++ "/restore/folder/a.txt",'text')^ == "original",restored.entries == 2,checked.verified]
}
