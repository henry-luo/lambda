import local: lambda.sync.local
import host: lambda.sync.document_host
import model: lambda.sync.document_state
import session: lambda.sync.session
import crypto: lambda.io.crypto
import workspace: lambda.sync.workspace

pn main() {
    let area = "temp/sync-document-repository-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/tree")^
    output("ab",area ++ "/tree/note")^
    output("",area ++ "/tree/empty")^
    output("{\"title\":\"old\",\"count\":1}",area ++ "/tree/data.json")^
    let secret = crypto.key()^
    let handle = local.create(area ++ "/repo.sqlite",secret,"document-repository",area ++ "/tree","writer")^
    let opened = host.open_document(handle,"note")^
    let empty = host.open_document(handle,"empty")^
    let edited = host.apply_edits(handle,opened.id,[{kind:'text',start:1,count:0,value:"L"}])^
    let restarted = local.open_repository(handle.path,secret,"document-repository")^
    let recovered = host.status(restarted,opened.id)^
    let disk_before = input(area ++ "/tree/note",'text')^
    let prepared = host.prepare_save(restarted,opened.id)^
    let saved = host.commit_save(local.open_repository(handle.path,secret,"document-repository")^,opened.id)^
    let record = model.get(local.status(restarted)^.state,opened.id)^
    let heads = workspace.entry(local.status(restarted)^.state.endpoint.replica,opened.entry)^.heads
    let history = local.history(restarted,opened.entry)^
    host.apply_edits(restarted,opened.id,[{kind:'text',start:0,count:0,value:"U"}])^
    output("aLb!",area ++ "/tree/note")^
    let external = host.refresh(restarted,opened.id)^
    host.compact(restarted,opened.id)^
    let final = host.status(local.open_repository(handle.path,secret,"document-repository")^,opened.id)^
    let structured = host.open_document(handle,"data.json",'mark','json')^
    host.apply_edits(handle,structured.id,[{kind:'set',key:"count",value:2}])^
    let json_saved = host.save(handle,structured.id)^
    let roundtrip = input(area ++ "/tree/data.json",'json')^
    let restored = host.restore_version(restarted,opened.id,record.origin.id)^
    let restored_save = host.save(restarted,opened.id)^
    let retained = model.get(local.status(restarted)^.state,opened.id)^.session.versions
    let checked = local.verify(handle)^
    io.delete(area)^
    return [edited.dirty,recovered.value == "aLb",recovered.coverage.local,disk_before == "ab",
        not saved.dirty,prepared.saved.frontier == heads[0].document.frontier,
        len(heads) == 1,len(history) == 2,record.session.base.id == saved.saved,
        external.value == "UaLb!",external.dirty,len(external.notices) > 0,
        final.value == external.value,not json_saved.dirty,roundtrip == {title:"old",count:2},
        restored.value == "ab",restored.dirty,not restored_save.dirty,
        any([for (version in retained) version.raw == "aLb"]),
        empty.coverage.local,not empty.coverage.cloud,not empty.coverage.peer,checked.verified]
}
