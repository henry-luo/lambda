import local: lambda.sync.local
import repo: lambda.sync.repository
import sqlite: lambda.io.sqlite
import crypto: lambda.io.crypto
import disk: lambda.io.disk
import hex: lambda.security.tuf.metadata

pn main() {
    let area = "temp/sync-local-maintenance"
    io.delete(area) ^ { null }
    io.mkdir(area ++ "/tree")^
    output("retained",area ++ "/tree/note.txt")^
    let secret = crypto.key()^
    let handle = local.create(area ++ "/repo.sqlite",secret,"maintenance",area ++ "/tree","device")^
    local.scan(handle)^
    let before = local.status(handle)^
    let interrupted = repo.access(handle.path,handle.keys,pn (tx,k)=> {
        repo.store_object(tx,k,"chunk",binary("unpublished orphan")^)^
        raise error("interrupted before head publication")
    }) ^ { true }
    let recovery = local.recover(handle)^
    let after = local.status(handle)^
    let available = local.inventory(handle)^
    sqlite.execute(handle.path,"DELETE FROM objects")^
    let unindexed = local.recover(handle)^
    let rebuilt = local.rebuild_index(handle)^
    let restored_inventory = local.inventory(handle)^
    let id = available[0]
    let directory = handle.path ++ ".objects"
    let name = hex.hex(id)^ ++ ".object"
    let captured = disk.capture(directory,name,8388608)^
    let bytes = captured.bytes
    let changed = binary([for (index in 0 to (len(bytes)^-1))
        if (index == len(bytes)^-1) bxor(bytes[index],1)^ else bytes[index]])^
    disk.install(directory,name,changed,captured.token)^
    let corrupt = local.verify(handle) ^ { true }
    let current = disk.inspect(directory,name)^
    disk.install(directory,name,bytes,current.token)^
    io.rename(directory ++ "/" ++ name,area ++ "/held.object")^
    let missing = local.verify(handle) ^ { true }
    io.rename(area ++ "/held.object",directory ++ "/" ++ name)^
    let checked = local.verify(handle)^;
    [interrupted,before.id == after.id,before.generation == after.generation,
     len(recovery.objects) == 1,not recovery.advanced_head,len(unindexed.candidates) > 0,
     rebuilt.rebuilt,all([for (item in available) contains(restored_inventory,item)^]),
     corrupt,missing,checked.verified,checked.unindexed == 0]
}
