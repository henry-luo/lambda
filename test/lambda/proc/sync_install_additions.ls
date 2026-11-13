import local: lambda.sync.local
import crypto: lambda.io.crypto
import h: ~~.mod_sync_local_helpers
pn main() {
    let area = "temp/sync-install-additions-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/a")^
    io.mkdir(area ++ "/b")^
    let key = crypto.key()^
    let a = local.create(area ++ "/a.sqlite",key,"additions",area ++ "/a","a")^
    let b = local.create(area ++ "/b.sqlite",key,"additions",area ++ "/b","b")^
    output("base",area ++ "/a/untouched.txt")^
    local.scan(a)^
    h.transfer(a,b)^
    local.materialize(b)^
    output("incoming",area ++ "/a/incoming.txt")^
    local.scan(a)^
    h.transfer(a,b)^
    local.prepare_install(b)^
    while (local.status(b)^.state.installation.next < len(local.status(b)^.state.installation.operations)^) {
        local.install_step(b)^
    }
    output("external addition",area ++ "/b/extra.txt")^
    output("external edit",area ++ "/b/untouched.txt")^
    let completed = local.install_step(b)^
    let imported = h.at_path(completed.state,"extra.txt")^
    h.transfer(b,a)^
    local.materialize(a)^
    let result = [completed.state.installation == null,completed.state.review == null,
        imported.type == 'file',input(area ++ "/a/extra.txt",'text')^ == "external addition",
        input(area ++ "/b/incoming.txt",'text')^ == "incoming",local.scan(b)^.id == completed.id,
        input(area ++ "/a/untouched.txt",'text')^ == "external edit"]
    output("next remote version",area ++ "/a/incoming.txt")^
    local.scan(a)^
    h.transfer(a,b)^
    local.prepare_install(b)^
    while (local.status(b)^.state.installation.next < len(local.status(b)^.state.installation.operations)^) {
        local.install_step(b)^
    }
    output("raced target",area ++ "/b/incoming.txt")^
    let blocked = local.install_step(b)^.state.installation
    let guarded = blocked != null and len(blocked.recoveries)^ > 0 and
        input(area ++ "/b/incoming.txt",'text')^ == "raced target"
    io.delete(area)^
    return [*result,guarded]
}
