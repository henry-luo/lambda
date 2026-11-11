import local: lambda.sync.local
import crypto: lambda.io.crypto
import h: ~~.mod_sync_local_helpers
import w: lambda.sync.workspace

pn main() {
    let area = "temp/sync-local-external"
    io.delete(area) ^ { null }
    io.mkdir(area ++ "/tree")^
    output("base",area ++ "/tree/item")^
    let secret = crypto.key()^
    let handle = local.create(area ++ "/repo.sqlite",secret,"external",area ++ "/tree","device")^
    let initial = local.scan(handle)^.state
    let id = h.at_path(initial,"item")^.id
    output("replacement",area ++ "/replacement")^
    io.rename(area ++ "/replacement",area ++ "/tree/item")^
    let replacement = local.scan(handle)^.state
    io.delete(area ++ "/tree/item")^
    io.mkdir(area ++ "/tree/item")^
    let review = local.scan(handle)^.state
    let resolved = local.resolve_review(handle,[
        {kind:'delete',entry:id},{kind:'create',type:'dir',parent:w.root,name:"item"}])^.state
    let before = local.status(handle)^
    io.symlink("outside",area ++ "/tree/link")^
    let rejected_link = local.scan(handle) ^ { true }
    let after = local.status(handle)^
    io.delete(area ++ "/tree/link")^
    let nested = local.create(area ++ "/tree/private.sqlite",secret,"bad",area ++ "/tree","bad") ^ { true };
    [h.at_path(replacement,"item")^.id == id,len(local.history(handle,id)^) == 2,
     review.review != null,resolved.review == null,h.at_path(resolved,"item")^.type == 'dir',
     h.at_path(resolved,"item")^.id != id,rejected_link,before.id == after.id,
     before.generation == after.generation,nested]
}
