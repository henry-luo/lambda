import local: lambda.sync.local
import host: lambda.sync.document_host
import maintenance: lambda.sync.maintenance
import retention: lambda.sync.retention
import history: lambda.sync.history
import model: lambda.sync.document_state
import repo: lambda.sync.repository
import crypto: lambda.io.crypto

pn main() {
    let area = "temp/sync-retention-repository-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/tree")^
    output("a",area ++ "/tree/note")^
    let key = crypto.key()^
    let handle = local.create(area ++ "/repo.sqlite",key,"retention-repository",area ++ "/tree","device")^
    let opened = host.open_document(handle,"note")^
    host.apply_edits(handle,opened.id,[{kind:'text',start:1,count:0,value:"b"}])^
    let b = host.save(handle,opened.id)^
    let snapshot = local.snapshot(handle,"b")^.state.snapshots[0]
    host.apply_edits(handle,opened.id,[{kind:'text',start:2,count:0,value:"c"}])^
    let c = host.save(handle,opened.id)^
    host.apply_edits(handle,opened.id,[{kind:'text',start:3,count:0,value:"d"}])^
    let d = host.save(handle,opened.id)^
    let one = host.versions(handle,opened.id,0,2)^
    let two = host.versions(handle,opened.id,one.next,2,one.root)^
    let packed = repo.access(handle.path,handle.keys,pn (tx,k)=> { return repo.load(tx,k)^.state })^
    maintenance.configure(handle,retention.daily("UTC",0,0))^
    let timestamp = now().unix+172800000
    let protected = maintenance.plan(handle,timestamp)^
    let b_protected = not contains(protected.versions,b.saved)^
    let c_candidate = contains(protected.versions,c.saved)^
    maintenance.pin(handle,[c.saved])^
    let stale = maintenance.forget(handle,protected.id,timestamp) ^ { ^ }
    let pinned = maintenance.plan(handle,timestamp)^
    let c_pinned = not contains(pinned.versions,c.saved)^
    maintenance.pin(handle,[c.saved],false)^
    let planned = maintenance.plan(handle,timestamp)^
    maintenance.forget(handle,planned.id,timestamp)^
    let listed = host.versions(handle,opened.id,0,32)^
    let c_retired = [for (version in listed.versions where version.id == c.saved) version][0].availability == 'retired'
    let prune = maintenance.plan_prune(handle)^
    var result = maintenance.prune_step(handle,prune.id,1)^
    let reopened = local.open_repository(handle.path,key,"retention-repository")^
    while (not result.complete) { result = maintenance.prune_step(reopened,prune.id,16)^ }
    let restored = host.restore_version(reopened,opened.id,b.saved)^
    let expired = host.restore_version(reopened,opened.id,c.saved) ^ { ^ }
    let verified = local.verify(reopened)^
    io.mkdir(area ++ "/restore")^
    let restored_snapshot = local.restore(reopened,snapshot,area ++ "/restore")^
    let bytes = input(area ++ "/restore/note",'text')^
    io.delete(area)^
    return [len(one.versions) == 2,len(two.versions) == 2,two.next == null,
        packed.documents[0].session.versions == [],len(packed.documents[0].history) == 1,
        b_protected,c_candidate,stale is error,c_pinned,c_retired,result.complete,
        restored.value == "ab",restored.dirty,expired is error,verified.verified,bytes == "ab"]
}
