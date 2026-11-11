// native local agent: policy and transactional orchestration stay in Lambda Script.
import sqlite: lambda.io.sqlite
import disk: lambda.io.disk
import paths: lambda.io.path
import repo: .repository
import chunks: .chunks
import cap: .workspace_capture
import fs: .filesystem
import install: .installation
import c: .core
import p: .protocol
import w: .workspace
import v: .schema
import codec: .codec
import tree: .workspace_tree
import blob: .blob

fn actor(handle, current) any^ => handle.writer ++ ":" ++ string(current.serial+1)^

fn validate(current, space) any^ {
    if (current.format != 1 or current.space != space or not v.actor(current.device)^ or
        not v.natural(current.serial)^) raise error("sync local: invalid local state")
    chunks.profile(current.chunker)^
    p.validate_endpoint(current.endpoint)^
    cap.validate_base(current.base)^
    if (current.endpoint.replica.document != space) raise error("sync local: wrong workspace")
    return true
}
pn replica_dependencies(tx, keys, replica, seen=[], depth=0) any^ {
    if (depth > 128) raise error("sync local: checkpoint dependency depth")
    let roots = {contents:blob.required(replica)^,checkpoints:blob.checkpoints(replica)^}
    var i = 0
    while (i < len(roots.contents)^) { repo.payload(tx,keys,roots.contents[i])^; i = i+1 }
    i = 0
    while (i < len(roots.checkpoints)^) {
        let digest = roots.checkpoints[i]
        if (not contains(seen,digest)^) {
            let nested = repo.get(tx,keys,repo.object_id(keys,"checkpoint",digest)^,"checkpoint")^
            c.validate(nested)^
            replica_dependencies(tx,keys,nested,[*seen,digest],depth+1)^
        }
        i = i+1
    }
    return true
}
pn dependencies(tx, keys, endpoint) any^ {
    return replica_dependencies(tx,keys,endpoint.replica)^
}

pub pn open_repository(path, secret, space) any^ {
    let handle = {path:path,keys:repo.keys(secret,space)^,writer:uuid()^}
    status(handle)^
    return handle
}

pub pn create(path, secret, space, root, device, config=chunks.default_profile) any^ {
    if (not v.actor(device)^) raise error("sync local: invalid device label")
    chunks.profile(config)^
    let directory = disk.resolve(root)^
    let parent = disk.resolve(paths.dirname(path)^)^
    let name = paths.basename(path)^
    let database = parent ++ "/" ++ name
    if (not (name is string) or name == "" or name == "." or name == ".." or
        paths.overlaps(database,directory)^ or paths.overlaps(database ++ ".objects",directory)^)
        raise error("sync local: repository must be outside the working tree")
    let handle = {path:database,keys:repo.keys(secret,space,database)^,writer:uuid()^}
    let endpoint = w.start(space)^
    let initial = {format:1,space:space,device:device,serial:0,chunker:config,root:directory,
        root_identity:disk.inspect(directory,"")^.identity,endpoint:endpoint,
        base:cap.baseline(endpoint.replica,[],cap.capture_id([])^)^,disk:[],review:null,
        installation:null,snapshots:[],previous:null}
    open tx = (sqlite.create(database)^) {
        repo.initialize(tx,handle.keys)^
        repo.commit_head(tx,handle.keys,0,initial)^
    }
    return handle
}

pub pn status(handle) any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let loaded = repo.load(tx,k)^
        validate(loaded.state,k.space)^
        return loaded
    })^
}

pn mutate(handle, operation) any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let loaded = repo.load(tx,k)^
        validate(loaded.state,k.space)^
        let updated = operation(tx,k,loaded.state)
        let next = updated^
        validate(next,k.space)^
        if (next == loaded.state) { return loaded }
        return repo.commit_head(tx,k,loaded.generation,{*:next,previous:loaded.id})^
    })^
}

pub pn scan(handle) any^ {
    return mutate(handle,pn (tx,k,current)=> {
        let captured = fs.capture(tx,k,current)^
        let updated = fs.reconciled(current,captured,actor(handle,current)^)^
        dependencies(tx,k,updated.endpoint)^
        return updated
    })^
}

pub pn resolve_review(handle, commands) any^ {
    return mutate(handle,pn (tx,k,current)=> {
        if (current.review == null) raise error("sync local: no external review")
        let captured = current.review.capture
        let base = p.start(current.base.checkpoint.replica,w.model)^
        let branch = cap.apply_groups(base,actor(handle,current)^,commands)^
        let members = fs.desired(branch)^
        if (cap.capture_id(members)^ != captured.id) raise error("sync local: resolution must represent captured disk contents")
        let endpoint = p.join_branch(current.endpoint,branch)^
        dependencies(tx,k,endpoint)^
        return {*:current,endpoint:endpoint,base:cap.baseline(branch.replica,members,captured.id)^,
            disk:[for (member in captured.members) {*:member,id:cap.association(members,member.path)^.id}],
            review:null,serial:current.serial+1}
    })^
}

pub pn transact(handle, commands) any^ {
    return mutate(handle,pn (tx,k,current)=> {
        let endpoint = w.transact(current.endpoint,actor(handle,current)^,commands)^.endpoint
        dependencies(tx,k,endpoint)^
        return {*:current,endpoint:endpoint,serial:current.serial+1}
    })^
}

pub pn publish(handle, entry, bytes, document=null) any^ {
    return mutate(handle,pn (tx,k,current)=> {
        let reference = repo.content(tx,k,if (bytes is string) binary(bytes)^ else bytes,current.chunker)^
        let endpoint = w.transact(current.endpoint,actor(handle,current)^,[
            {kind:'publish',entry:entry,content:reference,document:document}])^.endpoint
        dependencies(tx,k,endpoint)^
        return {*:current,endpoint:endpoint,serial:current.serial+1}
    })^
}

pub pn prepare_install(handle) any^ {
    return mutate(handle,pn (tx,k,current)=> {
        if (current.installation != null) { return current }
        let fresh = fs.reconciled(current,fs.capture(tx,k,current)^,actor(handle,current)^)^
        dependencies(tx,k,fresh.endpoint)^
        return install.prepare(fresh)^
    })^
}
pub pn install_step(handle) any^ {
    return mutate(handle,pn (tx,k,current)=> { return install.step(tx,k,current)^ })^
}
pub pn materialize(handle, max_steps=1000) any^ {
    if (not v.natural(max_steps)^ or max_steps > 100000) raise error("sync local: invalid installation budget")
    var result = prepare_install(handle)^
    var i = 0
    while (result.state.installation != null and i < max_steps) {
        result = install_step(handle)^
        if (result.state.installation.blocked != null) { return result }
        i = i+1
    }
    return result
}

pub pn inventory(handle) any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        return [for (row in sqlite.query(tx,"SELECT id FROM objects WHERE kind != 'state' ORDER BY id")^) row[0]]
    })^
}
pub pn export_objects(handle, ids) any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> { return repo.export_objects(tx,k,ids)^ })^
}
pub pn import_objects(handle, records) any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> { return repo.import_objects(tx,k,records)^ })^
}
pub pn messages(handle, peer_have) any^ {
    let current = status(handle)^.state
    let next = p.next(current.endpoint,peer_have)^
    return if (next.kind == 'checkpoint_required') p.frames(current.endpoint.replica,w.model,65536)^
        else [next]
}
pub pn receive(handle, packet, admission) any^ {
    return mutate(handle,pn (tx,k,current)=> {
        let endpoint = p.accept(current.endpoint,codec.encode(packet)^,admission)^
        dependencies(tx,k,endpoint)^
        return {*:current,endpoint:endpoint}
    })^
}

pub pn acknowledge(handle, peer, frontier) any^ {
    return mutate(handle,pn (tx,k,current)=> {
        return {*:current,endpoint:p.acknowledge(current.endpoint,peer,frontier)^}
    })^
}

pub pn snapshot(handle, label) any^ {
    return mutate(handle,pn (tx,k,current)=> {
        dependencies(tx,k,current.endpoint)^
        let saved = {version:1,label:label,workspace:c.snapshot(current.endpoint.replica)^}
        let id = repo.store_object(tx,k,"snapshot",saved)^
        return {*:current,snapshots:unique([*current.snapshots,id])}
    })^
}

pub pn register_checkpoint(handle, saved) any^ {
    let replica = c.restore(saved)^
    let link = w.document(saved)^
    repo.access(handle.path,handle.keys,pn (tx,k)=> {
        repo.store_object(tx,k,"checkpoint",p.checkpoint(replica))^
        return true
    })^
    return link
}

pub pn history(handle, entry) any^ {
    let current = status(handle)^.state
    return tree.revisions(tree.events(current.endpoint.replica)^,entry)^
}

pub pn read_content(handle, reference) any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> { return repo.payload(tx,k,reference)^ })^
}

pub pn restore(handle, snapshot, destination) any^ {
    let target = disk.resolve(destination)^
    let current = status(handle)^.state
    if (paths.overlaps(target,current.root)^ or paths.overlaps(target,handle.path)^ or
        paths.overlaps(target,handle.path ++ ".objects")^) raise error("sync local: restore requires a separate target")
    if (len(disk.entries(target,"",10000)^) != 0) raise error("sync local: restore target must be empty")
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let saved = repo.get(tx,k,snapshot,"snapshot")^
        let replica = c.restore(saved.workspace)^
        let desired = fs.desired(p.start(replica,w.model)^)^
        var i = 0
        while (i < len(desired)) {
            let member = desired[i]
            if (member.type == 'dir') { disk.directory(target,member.path)^ }
            else { disk.install(target,member.path,repo.payload(tx,k,member.content)^,null)^ }
            i = i+1
        }
        return {snapshot:snapshot,entries:len(desired),destination:target}
    })^
}

pub pn verify(handle) any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let checked = repo.verify(tx,k)^
        let loaded = repo.load(tx,k)^
        validate(loaded.state,k.space)^
        dependencies(tx,k,loaded.state.endpoint)^
        if (loaded.state.previous != null) { repo.record(k,loaded.state.previous)^ }
        let snapshots = sqlite.query(tx,"SELECT id FROM objects WHERE kind = 'snapshot'")^
        var i = 0
        while (i < len(snapshots)) {
            let saved = repo.get(tx,k,snapshots[i][0],"snapshot")^
            v.fields(saved,["version","label","workspace"])^
            if (saved.version != 1) raise error("sync local: unsupported snapshot")
            replica_dependencies(tx,k,c.restore(saved.workspace)^)^
            i = i+1
        }
        return checked
    })^
}

pub pn rebuild_index(handle) any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let rebuilt = repo.rebuild_index(tx,k)^
        repo.verify(tx,k)^
        return rebuilt
    })^
}

pub pn recover(handle) any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let found = repo.recover(tx,k)^
        var candidates = []
        var i = 0
        while (i < len(found)) {
            let item = found[i]
            if (item.kind == "state") {
                let candidate = repo.get(tx,k,item.id,"state")^
                validate(candidate,k.space)^
                dependencies(tx,k,candidate.endpoint)^
                candidates = [*candidates,item.id]
            }
            i = i+1
        }
        return {objects:found,candidates:candidates,advanced_head:false}
    })^
}
