import local: lambda.sync.local
import install: lambda.sync.installation
import crypto: lambda.io.crypto
import h: ~~.mod_sync_local_helpers
pn main() {
    let area = "temp/sync-install-batch-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/a")^; io.mkdir(area ++ "/b")^
    let secret = crypto.key()^
    let a = local.create(area ++ "/a.sqlite",secret,"install-batch",area ++ "/a","a")^
    let b = local.create(area ++ "/b.sqlite",secret,"install-batch",area ++ "/b","b")^
    for (name in ["one","two","three"]) { output(name,area ++ "/a/" ++ name)^ }
    local.scan(a)^; h.transfer(a,b)^
    let prepared = local.prepare_install(b)^
    // Files survive a rolled-back progress transaction; the earlier complete intent remains durable.
    let interrupted = local.mutate(b,pn (tx,k,current)=> {
        let first = install.step(tx,k,current,"recovery")^
        install.step(tx,k,first,"recovery")^
        raise error("simulated progress rollback")
    }) ^ { ^ }
    let held = local.status(b)^
    let recovered = local.materialize(b)^
    let replayed = interrupted is error and held.generation == prepared.generation and
        held.state.installation.next == 0 and recovered.state.installation == null and
        all([for (name in ["one","two","three"]) input(area ++ "/b/" ++ name,'text')^ == name])
    output("remote next",area ++ "/a/one")^
    local.scan(a)^; h.transfer(a,b)^
    local.prepare_install(b)^
    local.mutate(b,pn (tx,k,current)=> {
        install.step(tx,k,current,"recovery")^
        raise error("simulated progress rollback")
    }) ^ { null }
    output("external concurrent edit",area ++ "/b/one")^
    let blocked = local.materialize(b)^
    let guarded = blocked.state.installation.blocked != null and
        len(blocked.state.installation.recoveries)^ > 0 and
        input(area ++ "/b/one",'text')^ == "external concurrent edit"
    io.delete(area)^
    return replayed and guarded
}
