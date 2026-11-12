import local: lambda.sync.local
import maintenance: lambda.sync.maintenance
import retention: lambda.sync.retention
import repo: lambda.sync.repository
import core: lambda.sync.core
import workspace: lambda.sync.workspace
import helpers: ~~.mod_sync_local_helpers
import crypto: lambda.io.crypto

pn main() {
    let area = "temp/sync-retention-offline-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/a")^
    io.mkdir(area ++ "/b")^
    output("base",area ++ "/a/file")^
    let secret = crypto.key()^
    let a = local.create(area ++ "/a.sqlite",secret,"retention-offline",area ++ "/a","a")^
    let b = local.create(area ++ "/b.sqlite",secret,"retention-offline",area ++ "/b","b")^
    let initial = local.scan(a)^.state
    let entry = helpers.at_path(initial,"file")^
    let base = entry.heads[0]
    helpers.transfer(a,b)^
    local.materialize(b)^
    output("offline",area ++ "/b/file")^
    local.scan(b)^
    output("middle",area ++ "/a/file")^
    let middle = local.scan(a)^.state
    let reference = helpers.at_path(middle,"file")^.heads[0].content
    output("current",area ++ "/a/file")^
    local.scan(a)^
    let orphan = repo.access(a.path,a.keys,pn (tx,k)=> {
        repo.store_object(tx,k,"snapshot",{version:1,label:"unpublished recovery",workspace:core.snapshot(middle.endpoint.replica)^})^
        raise error("interrupt before publishing the recovery root")
    }) ^ { ^ }
    maintenance.configure(a,retention.daily("UTC",0,1000))^
    let timestamp = now().unix+172800000
    let plan = maintenance.plan(a,timestamp)^
    let early = maintenance.ready(a,plan.id,timestamp)^
    let rejected = maintenance.forget(a,plan.id,timestamp) ^ { ^ }
    maintenance.forget(a,plan.id,timestamp+1000)^
    let expired = maintenance.file_history(a,entry.id)^
    let prune = maintenance.plan_prune(a)^
    var swept = maintenance.prune_step(a,prune.id,128)^
    while (not swept.complete) { swept = maintenance.prune_step(a,prune.id,128)^ }
    let missing_base = local.read_content(a,base.content) ^ { ^ }
    let orphan_bytes = local.read_content(a,reference)^
    let current = local.status(a)^.state
    let manifest = current.retention.manifest
    local.import_objects(b,local.export_objects(a,[manifest])^)^
    local.register_retention(b,manifest)^
    helpers.transfer(a,b)^
    helpers.transfer(b,a)^
    let heads = workspace.entry(local.status(a)^.state.endpoint.replica,entry.id)^.heads
    let contents = [for (head in heads) local.read_content(a,head.content)^]
    let after_return = local.read_content(a,base.content) ^ { ^ }
    let before_recovery = local.status(a)^
    let recovered = local.recover(a)^
    let checked = local.verify(a)^
    let after_recovery = local.status(a)^
    io.delete(area)^
    return [base.saved_at is number,orphan is error,not early,rejected is error,
        len(plan.revisions) == 2,len([for (version in expired.versions where version.availability == 'retired') version]) == 2,
        missing_base is error,orphan_bytes == binary("middle")^,len(heads) == 2,
        contains(contents,binary("offline")^)^,contains(contents,binary("current")^)^,
        after_return is error,len(recovered.objects) > 0,not recovered.advanced_head,
        checked.verified,before_recovery.id == after_recovery.id,
        all([for (head in heads) head.saved_at is number])]
}
