import local: lambda.sync.local
import host: lambda.sync.document_host
import crypto: lambda.io.crypto
pn main() {
    let area = "temp/sync-document-gc-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/tree")^
    output("a",area ++ "/tree/note")^
    let secret = crypto.key()^
    let handle = local.create(area ++ "/repo.sqlite",secret,"document-gc",area ++ "/tree","owner")^
    let opened = host.open_document(handle,"note")^
    host.apply_edits(handle,opened.id,[{kind:'text',start:1,count:0,value:"B"}])^
    let recovered = host.status(local.open_repository(handle.path,secret,"document-gc")^,opened.id)^
    let before = input(area ++ "/tree/note",'text')^
    let saved = host.save(handle,opened.id)^
    let after = input(area ++ "/tree/note",'text')^
    io.delete(area)^
    return [recovered.value == "aB",recovered.dirty,recovered.coverage.local,
        not recovered.coverage.cloud,before == "a",after == "aB",not saved.dirty]
}
