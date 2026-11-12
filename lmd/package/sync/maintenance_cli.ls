// host-authorized maintenance requests share the existing public-request/private-key envelope.
import agent: .peer_agent
import host: .maintenance
import local: .local
import repo: .repository
import policy: .retention
import cli: .cli
import hex: lambda.security.tuf.metadata

fn configured(value) any^ => {*:value,mode:symbol(value.mode),
    rules:[for (rule in value.rules) {*:rule,unit:symbol(rule.unit)}]}
pn execute(request, key, environment) any^ {
    let owner = agent.open_agent(request.directory,key)^
    let handle = owner.handle
    let action = request.action
    if (action == "configure") {
        return host.configure(handle,configured(request.policy)^,
            [for (entry in (request.overrides or [])) {id:entry.id,policy:configured(entry.policy)^}])^.state.retention
    }
    if (action == "pin") {
        return host.pin(handle,[for (id in request.ids) hex.unhex(id)^],request.enabled)^.state.retention
    }
    if (action == "file_history") {
        return host.file_history(handle,request.entry,request.cursor or 0,request.limit or 32,
            if (request.root == null) null else hex.unhex(request.root)^)^
    }
    if (action == "restore_file") { host.restore_file(handle,request.entry,hex.unhex(request.version)^)^; return true }
    if (action == "scan") { local.scan(handle)^; return true }
    if (action == "headers") {
        return repo.access(handle.path,handle.keys,pn (tx,k)=> { return host.headers(tx,k,repo.load(tx,k)^.state)^ })^
    }
    if (action == "snapshot") { return local.snapshot(handle,request.label)^.state.snapshots }
    if (action == "verify") { return local.verify(handle)^ }
    if (action == "release_recovery") { return host.release_recovery(handle,[for (id in request.ids) hex.unhex(id)^])^.state.recovery_pins }
    if (action == "release_snapshot") { return host.release_snapshot(handle,hex.unhex(request.snapshot)^)^.state.snapshots }
    if (action == "plan") { return host.plan(handle,request.at or now().unix)^ }
    if (action == "forget") { return host.forget(handle,hex.unhex(request.id)^,request.at or now().unix)^.state.retention }
    if (action == "plan_prune") { return host.plan_prune(handle)^ }
    if (action == "prune_step") { return host.prune_step(handle,hex.unhex(request.id)^,request.budget or 16)^ }
    raise error("sync maintenance: unknown command")
}
pn main() { return cli.invoke(execute,"LAMBDA_SYNC_RESULT")^ }
