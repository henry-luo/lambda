import local: lambda.sync.local
import crypto: lambda.io.crypto
import t: lambda.sync.text
import s: lambda.sync.session
import h: ~~.mod_sync_local_helpers
import repo: lambda.sync.repository

pn main() {
    let area = "temp/sync-local-document"
    io.delete(area) ^ { null }
    io.mkdir(area ++ "/tree")^
    output("ab",area ++ "/tree/note.txt")^
    let secret = crypto.key()^
    let handle = local.create(area ++ "/repo.sqlite",secret,"documents",area ++ "/tree","device")^
    let current = local.scan(handle)^.state
    let id = h.at_path(current,"note.txt")^.id
    let opened = s.start(t.from_text("note","author","ab")^)^
    let edited = s.update(opened,t.insert(opened.live,"author",1,"A")^)^
    let prepared = s.prepare_save(edited)^
    let link = local.register_checkpoint(handle,prepared.saved)^
    local.publish(handle,id,prepared.saved.raw,link)^
    let later = s.update(edited,t.insert(edited.live,"author",2,"later")^)^
    let head = h.at_path(local.status(handle)^.state,"note.txt")^.heads[0]
    let restored = repo.access(handle.path,handle.keys,pn (tx,k)=> {
        return repo.get(tx,k,repo.object_id(k,"checkpoint",link.checkpoint)^,"checkpoint")^
    })^
    local.materialize(handle)^
    let checked = local.verify(handle)^;
    [head.document == link,link.frontier == prepared.saved.frontier,
     t.read(restored)^ == "aAb",t.read(later.live)^ == "aAlaterb",
     input(area ++ "/tree/note.txt",'text')^ == "aAb",len(local.history(handle,id)^) == 2,checked.verified]
}
