import local: lambda.sync.local
import crypto: lambda.io.crypto
import h: ~~.mod_sync_local_helpers
pn main() {
    let area = "temp/sync-event-capture-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/tree/a/sub")^
    io.mkdir(area ++ "/tree/b")^
    output("first",area ++ "/tree/a/sub/file.txt")^
    output("stable",area ++ "/tree/b/stable.txt")^
    let repo = local.create(area ++ "/repo.sqlite",crypto.key()^,"events",area ++ "/tree","device")^
    local.scan(repo)^
    let id = h.at_path(local.status(repo)^.state,"a/sub/file.txt")^.id
    output("edited",area ++ "/tree/a/sub/file.txt")^
    let edited = local.scan(repo,["a/sub/file.txt"])^.id
    let exact = local.scan(repo)^.id == edited
    io.move(area ++ "/tree/a",area ++ "/tree/moved")^
    let moved = local.scan(repo,["a","moved"])^.id
    let same_id = h.at_path(local.status(repo)^.state,"moved/sub/file.txt")^.id == id
    let exact_move = local.scan(repo)^.id == moved
    output("new",area ++ "/tree/b/new.txt")^
    let added = local.scan(repo,["b/new.txt"])^.id
    io.delete(area ++ "/tree/b/new.txt")^
    let removed = local.scan(repo,["b/new.txt"])^.id
    let exact_delete = local.scan(repo)^.id == removed
    let invalid = local.scan(repo,["../outside"]) ^ { ^ }
    let intact = local.status(repo)^.id == removed
    output("missed event",area ++ "/tree/b/stable.txt")^
    let unhinted = local.scan(repo,["moved/sub/file.txt"])^.id
    let verified = local.scan(repo,null,true)^.id
    io.delete(area)^
    return [exact,same_id,exact_move,added != removed,exact_delete,invalid is error,intact,
        unhinted == removed and verified != unhinted]
}
