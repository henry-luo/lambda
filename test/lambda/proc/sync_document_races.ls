import local: lambda.sync.local
import host: lambda.sync.document_host
import model: lambda.sync.document_state
import crypto: lambda.io.crypto
import workspace: lambda.sync.workspace
import c: lambda.sync.core
import t: lambda.sync.text
pn main() {
    let area = "temp/sync-document-races-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/tree")^
    output("ab",area ++ "/tree/note")^
    let secret = crypto.key()^
    let h = local.create(area ++ "/repo.sqlite",secret,"document-races",area ++ "/tree","writer")^
    let opened = host.open_document(h,"note")^
    let commands = [{kind:'text',start:1,count:0,value:"A"}]
    host.apply_edits(h,opened.id,commands,false,"edit-1")^
    let retried = host.apply_edits(local.open_repository(h.path,secret,"document-races")^,opened.id,commands,false,"edit-1")^
    let fork = host.apply_edits(h,opened.id,[{kind:'text',start:0,count:0,value:"bad"}],false,"edit-1") ^ { ^ }
    let intent = host.prepare_save(h,opened.id,"save-1")^
    host.apply_edits(h,opened.id,[{kind:'text',start:2,count:0,value:"B"}])^
    host.install_save(h,opened.id)^
    let reopened = local.open_repository(h.path,secret,"document-races")^
    let pending = host.status(reopened,opened.id)^
    let saved = host.commit_save(reopened,opened.id)^
    let captured = input(area ++ "/tree/note",'text')^
    let entry = workspace.entry(local.status(h)^.state.endpoint.replica,opened.entry)^
    let repeated_save = host.save(h,opened.id,"save-1")^
    let repeated_history = local.history(h,opened.entry)^
    host.prepare_save(h,opened.id)^
    output("aAb!",area ++ "/tree/note")^
    let raced = host.commit_save(h,opened.id) ^ { ^ }
    let held = host.status(h,opened.id)^
    host.cancel_save(h,opened.id)^
    let merged = host.refresh(h,opened.id)^
    let old = model.get(local.status(h)^.state,opened.id)^.session.base.replica
    let offline = t.insert(old,"offline-peer",0,"O")^
    host.compact(h,opened.id)^
    let compacted = model.get(local.status(h)^.state,opened.id)^
    let future = c.merge(compacted.endpoint.replica,offline)^
    let renamed = io.rename(area ++ "/tree/note",area ++ "/tree/moved")^
    let moved = host.refresh(h,opened.id)^
    io.delete(area ++ "/tree/moved")^
    let deleted = host.refresh(h,opened.id)^
    let rejected = host.prepare_save(h,opened.id) ^ { ^ }
    io.delete(area)^
    return [pending.prepared,saved.dirty,saved.value == "aABb",captured == "aAb",
        entry.heads[0].document.frontier == intent.saved.frontier,len(entry.heads) == 1,
        raced is error,held.prepared,merged.value == "aABb!",merged.dirty,
        t.read(future)^ == "OaABb!",moved.path == "moved",moved.value == merged.value,
        deleted.deleted,deleted.value == merged.value,rejected is error,
        retried.value == "aAb",fork is error,repeated_save.saved == saved.saved,len(repeated_history) == 2]
}
