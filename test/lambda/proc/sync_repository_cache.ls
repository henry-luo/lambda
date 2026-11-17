import local: lambda.sync.local
import host: lambda.sync.document_host
import workspace: lambda.sync.workspace
import crypto: lambda.io.crypto
import disk: lambda.io.disk
import h: ~~.mod_sync_local_helpers
pn main() {
    let area = "temp/sync-repository-cache-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/tree")^
    output("ab",area ++ "/tree/note")^
    let key = crypto.key()^
    let raw = local.create(area ++ "/repo.sqlite",key,"cache-regression",area ++ "/tree","cache")^
    let checked = h.owned([raw],pn (handles)=> {
        let handle = handles[0]
        let opened = host.open_document(handle,"note")^
        let edited = host.apply_edits(handle,opened.id,[{kind:'text',start:1,count:0,value:"L"}])^
        let first = local.status(handle)^
        let stable = local.mutate(handle,pn (tx,k,current)=> { return current })^
        let again = local.status(handle)^
        var steady = true
        for (i in 1 to 20) {
            let observed = local.status(handle)^
            steady = steady and observed.generation == again.generation and
                observed.repository_cache.expanded_bytes == again.repository_cache.expanded_bytes
        }
        let wrong_type = local.mutate(handle,pn (tx,k,current)=> {
            return {*:current,serial:float(current.serial)^}
        }) ^ { ^ }
        let held = local.status(handle)^
        let cold = local.open_repository(raw.path,key,"cache-regression")^
        local.transact(cold,[{kind:'create',type:'dir',parent:workspace.root,name:"external-dir"}])^
        let advanced = local.status(handle)^
        let saved = host.save(handle,opened.id)^
        let restored = host.status(local.open_repository(raw.path,key,"cache-regression")^,opened.id)^
        let current = local.status(handle)^
        let filename = h.physical(handle,current.id)^
        let original = disk.capture(raw.path ++ ".objects",filename,16777216)^
        disk.install(raw.path ++ ".objects",filename,binary("damaged pack")^,original.token)^
        let invalid = local.verify(handle) ^ { ^ }
        disk.install(raw.path ++ ".objects",filename,original.bytes,disk.inspect(raw.path ++ ".objects",filename)^.token)^
        let verified = local.verify(handle)^
        let padding = join([for (i in 1 to 1024) join([for (j in 1 to 1024) "x"],"")^],"")^
        let large = local.mutate(handle,pn (tx,k,value)=> { return {*:value,test_padding:padding} })^
        return [edited.value == "aLb" and edited.dirty,first.repository_cache.expanded != null,
            first.repository_cache.expanded_bytes <= 1048576,stable.generation == first.generation,
            again.generation == stable.generation,steady,wrong_type is error,held.generation == again.generation,
            advanced.generation > held.generation,h.at_path(advanced.state,"external-dir")^.type == 'dir',
            not saved.dirty,restored.value == "aLb" and not restored.dirty,invalid is error,verified.verified,
            large.repository_cache.expanded == null,large.repository_cache.expanded_bytes > 1048576]
    })^
    io.delete(area)^
    return checked
}
