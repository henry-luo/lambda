import local: lambda.sync.local
import crypto: lambda.io.crypto
import h: ~~.mod_sync_local_helpers
import w: lambda.sync.workspace

pn main() {
    let area = "temp/sync-local-recovery"
    io.delete(area) ^ { null }
    io.mkdir(area ++ "/a")^
    io.mkdir(area ++ "/b")^
    output("base",area ++ "/a/note.txt")^
    let secret = crypto.key()^
    let a = local.create(area ++ "/a.sqlite",secret,"recovery",area ++ "/a","Alice")^
    let b = local.create(area ++ "/b.sqlite",secret,"recovery",area ++ "/b","Bob")^
    let original = local.scan(a)^.state
    let id = h.at_path(original,"note.txt")^.id
    h.transfer(a,b)^
    local.materialize(b)^
    output("alice",area ++ "/a/note.txt")^
    output("bob",area ++ "/b/note.txt")^
    local.scan(a)^
    local.scan(b)^
    h.transfer(a,b)^
    h.transfer(b,a)^
    let conflict = w.entry(local.status(a)^.state.endpoint.replica,id)^
    let refused = local.prepare_install(a) ^ { true }
    let versions = local.history(a,id)^
    let values = [for (revision in versions) local.read_content(a,revision.content)^]
    local.publish(a,id,"merged")^
    h.transfer(a,b)^
    local.prepare_install(b)^
    output("raced",area ++ "/b/note.txt")^
    let blocked = local.install_step(b)^.state.installation
    let reopened = local.open_repository(b.path,secret,"recovery")^
    let survived = local.status(reopened)^.state.installation
    let captured = local.read_content(reopened,survived.recoveries[0].content)^
    output("bob",area ++ "/b/note.txt")^
    let resumed = local.materialize(reopened)^
    let before = local.status(reopened)^
    io.rename(area ++ "/b",area ++ "/moved")^
    let missing_root = local.scan(reopened) ^ { true }
    let after = local.status(reopened)^
    io.rename(area ++ "/moved",area ++ "/b")^
    let final = local.scan(reopened)^;
    [len(conflict.heads) == 2,refused,len(versions) == 3,
     contains(values,binary("alice")^)^ and contains(values,binary("bob")^)^,
     blocked.blocked.reason == "external change before write",survived == blocked,captured == binary("raced")^,
     resumed.state.installation == null,input(area ++ "/b/note.txt",'text')^ == "merged",
     missing_root,before.id == after.id,before.generation == after.generation,
     len(w.entry(final.state.endpoint.replica,id)^.heads) == 1]
}
