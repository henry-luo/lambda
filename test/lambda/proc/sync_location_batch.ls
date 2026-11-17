import local: lambda.sync.local
import repo: lambda.sync.repository
import locations: lambda.sync.location_index
import crypto: lambda.io.crypto
import hex: lambda.security.tuf.metadata
pn main() {
    let area = "temp/sync-locations-batch-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/tree")^
    let secret = crypto.key()^
    let handle = local.create(area ++ "/repo.sqlite",secret,"locations",area ++ "/tree","device")^
    let ids = [for (i in 0 to 159) hash("location-" ++ string(i),'sha256')^]
    let initial = [for (i in 0 to 89) {id:ids[i],key:"objects/" ++ hex.hex(ids[i])^}]
    let updates = [for (i in 60 to 149) {id:ids[i],key:"objects/1/" ++ hex.hex(ids[i])^}]
    local.mutate(handle,pn (tx,k,current)=> { return locations.update(tx,k,current,"provider",initial)^ })^
    local.mutate(handle,pn (tx,k,current)=> { return locations.update(tx,k,current,"provider",updates)^ })^
    let reopened = local.open_repository(handle.path,secret,"locations")^
    let ordered = [for (i in 0 to 159) ids[159-i]]
    let got = local.cloud_locations(reopened,"provider",ordered)^
    let expected = repo.access(reopened.path,reopened.keys,pn (tx,k)=> {
        let current = repo.load(tx,k)^.state
        return [for (id in ordered) {id:id,key:locations.lookup(tx,k,current,"provider",id)^}]
    })^
    let located = [for (item in got where item.key != null) item]
    let selected = [for (item in got where item.id == ids[60]) item]
    let retained = [for (item in got where item.id == ids[0]) item]
    io.delete(area)^
    return [got == expected,len(located)^ == 150,selected[0].key == updates[0].key,
        retained[0].key == initial[0].key,len([for (item in got where item.key == null) item])^ == 10]
}
