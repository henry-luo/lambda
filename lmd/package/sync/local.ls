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
import documents: .documents
import maintenance: .maintenance_state
import reach: .reachability
import cache_state: .cache_state
import command_model: .workspace_commands
import document_state: .document_state

fn actor(handle, current) any^ => handle.writer ++ ":" ++ string(current.serial+1)^

fn validate(current, space) any^ {
    if (current.format != 1 or current.space != space or not v.actor(current.device)^ or
        not v.natural(current.serial)^) raise error("sync local: invalid local state")
    chunks.profile(current.chunker)^
    p.validate_endpoint(current.endpoint)^
    cap.validate_base(current.base)^
    if (current.endpoint.replica.document != space) raise error("sync local: wrong workspace")
    for (record in document_state.all(current)) { document_state.validate(record,space)^ }
    return true
}
pn replica_dependencies(tx, keys, replica, seen=[], depth=0, retired=[], source="workspace", cached=[]) any^ {
    if (depth > 128) raise error("sync local: checkpoint dependency depth")
    if (replica.profile == 'workspace' and any([for (id in maintenance.live_revisions(replica)^) contains(retired,id)^]))
        raise error("sync local: " ++ source ++ ": retired live revision requires explicit recovery")
    let roots = {contents:maintenance.contents(replica,retired)^,checkpoints:maintenance.checkpoints(replica,retired)^}
    var i = 0
    if (replica.profile == 'workspace') {
        let heads = maintenance.live_revisions(replica)^
        for (revision in maintenance.revisions(replica)^ where contains(heads,revision.id)^) { repo.payload(tx,keys,revision.content)^ }
    }
    while (i < len(roots.contents)^) { repo.availability(tx,keys,roots.contents[i],cached)^; i = i+1 }
    i = 0
    while (i < len(roots.checkpoints)^) {
        let digest = roots.checkpoints[i]
        if (not contains(seen,digest)^) {
            let nested = repo.get(tx,keys,repo.object_id(keys,"checkpoint",digest)^,"checkpoint")^
            c.validate(nested)^
            replica_dependencies(tx,keys,nested,[*seen,digest],depth+1,retired,source,cached)^
        }
        i = i+1
    }
    return true
}
pn dependencies(tx, keys, endpoint, retired=[], source="workspace", cached=[]) any^ {
    return replica_dependencies(tx,keys,endpoint.replica,[],0,retired,source,cached)^
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
        let expanded = documents.expand(tx,k,loaded.state)^
        validate(expanded,k.space)^
        return {*:loaded,state:expanded}
    })^
}

pub pn mutate(handle, operation) any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let loaded = repo.load(tx,k)^
        let expanded = documents.expand(tx,k,loaded.state)^
        validate(expanded,k.space)^
        let updated = operation(tx,k,expanded)
        let next = documents.flush(tx,k,updated^)^
        validate(next,k.space)^
        if (next == loaded.state) { return {*:loaded,state:expanded} }
        let committed = repo.commit_head(tx,k,loaded.generation,{*:next,previous:loaded.id})^
        return {*:committed,state:documents.expand(tx,k,committed.state)^}
    })^
}

pub pn scan(handle) any^ {
    return mutate(handle,pn (tx,k,current)=> {
        let captured = fs.capture(tx,k,current)^
        let updated = fs.reconciled(current,captured,actor(handle,current)^,[],now().unix)^
        dependencies(tx,k,updated.endpoint,maintenance.get(updated).revisions,"scan",reach.cache_ids(updated))^
        return documents.refresh(tx,k,updated,actor(handle,updated)^)^
    })^
}

pub pn resolve_review(handle, commands) any^ {
    return mutate(handle,pn (tx,k,current)=> {
        if (current.review == null) raise error("sync local: no external review")
        let captured = current.review.capture
        let base = p.start(current.base.checkpoint.replica,w.model)^
        let branch = cap.apply_groups(base,actor(handle,current)^,command_model.stamp(commands,now().unix)^)^
        let members = fs.desired(branch)^
        if (cap.capture_id(members)^ != captured.id) raise error("sync local: resolution must represent captured disk contents")
        let endpoint = p.join_branch(current.endpoint,branch)^
        dependencies(tx,k,endpoint,maintenance.get(current).revisions,"workspace",reach.cache_ids(current))^
        return {*:current,endpoint:endpoint,base:cap.baseline(branch.replica,members,captured.id)^,
            disk:[for (member in captured.members) {*:member,id:cap.association(members,member.path)^.id}],
            review:null,serial:current.serial+1}
    })^
}

pub pn transact(handle, commands) any^ {
    return mutate(handle,pn (tx,k,current)=> {
        let endpoint = w.transact(current.endpoint,actor(handle,current)^,command_model.stamp(commands,now().unix)^)^.endpoint
        dependencies(tx,k,endpoint,maintenance.get(current).revisions,"workspace",reach.cache_ids(current))^
        return {*:current,endpoint:endpoint,serial:current.serial+1}
    })^
}

pub pn publish(handle, entry, bytes, document=null) any^ {
    return mutate(handle,pn (tx,k,current)=> {
        let reference = repo.content(tx,k,if (bytes is string) binary(bytes)^ else bytes,current.chunker)^
        let endpoint = w.transact(current.endpoint,actor(handle,current)^,[
            {kind:'publish',entry:entry,content:reference,document:document,saved_at:now().unix}])^.endpoint
        dependencies(tx,k,endpoint,maintenance.get(current).revisions,"workspace",reach.cache_ids(current))^
        return {*:current,endpoint:endpoint,serial:current.serial+1}
    })^
}

pub pn prepare_install(handle) any^ {
    return mutate(handle,pn (tx,k,current)=> {
        if (current.installation != null) { return current }
        let fresh = fs.reconciled(current,fs.capture(tx,k,current)^,actor(handle,current)^,[],now().unix)^
        dependencies(tx,k,fresh.endpoint,maintenance.get(fresh).revisions,"install preparation",reach.cache_ids(fresh))^
        return install.prepare(fresh)^
    })^
}
pub pn install_step(handle) any^ {
    return mutate(handle,pn (tx,k,current)=> {
        let updated = install.step(tx,k,current)^
        return if (updated.installation != null) updated else documents.refresh(tx,k,updated,actor(handle,updated)^)^
    })^
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

pub pn inventory(handle, include_documents=true) any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let current = repo.load(tx,k)^.state
        let rows = sqlite.query(tx,"SELECT id, kind FROM objects WHERE kind NOT IN ('state', 'maintenance') ORDER BY id")^
        let selected = if (maintenance.get(current).manifest == null) [for (row in rows) row[0]]
            else reach.roots(tx,k,current,false)^
        return [for (row in rows where contains(selected,row[0])^ and (include_documents or row[1] != "document")) row[0]]
    })^
}
pub pn export_objects(handle, ids) any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> { return repo.export_objects(tx,k,ids)^ })^
}
pub pn import_objects(handle, records) any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        repo.import_objects(tx,k,records)^
        let loaded = repo.load(tx,k)^
        let current = loaded.state
        let staged = if (current.staged_objects == null) [] else current.staged_objects
        repo.commit_head(tx,k,loaded.generation,{*:current,previous:loaded.id,
            staged_objects:sort(unique([*staged,*[for (record in records) record.id]]))^})^
        return true
    })^
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
        // partial frames retain the old branch while retirement precedes checkpoint installation.
        if (endpoint.replica != current.endpoint.replica) {
            dependencies(tx,k,endpoint,maintenance.get(current).revisions,"receive",reach.cache_ids(current))^
        }
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
        dependencies(tx,k,current.endpoint,maintenance.get(current).revisions,"snapshot creation",reach.cache_ids(current))^
        let saved = {version:1,label:label,workspace:c.snapshot(current.endpoint.replica)^}
        let id = repo.store_object(tx,k,"snapshot",saved)^
        if (contains(maintenance.get(current).snapshots,id)^) raise error("sync local: snapshot identity retired; select a new label")
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
    mutate(handle,pn (tx,k,current)=> {
        repo.get(tx,k,snapshot,"snapshot")^
        return {*:current,staged_objects:unique([*reach.staged(current),snapshot])}
    })^
    let result = repo.access(handle.path,handle.keys,pn (tx,k)=> {
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
    release_staging(handle,[snapshot])^
    return result
}

pub pn verify(handle) any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let raw = repo.load(tx,k)^
        let loaded = {*:raw,state:documents.expand(tx,k,raw.state)^}
        let plan = loaded.state.maintenance
        let pending = if (plan == null or plan.kind != 'prune' or plan.fingerprint != maintenance.fingerprint(loaded.state)^) []
            else plan.objects
        let checked = repo.verify(tx,k,reach.cache_ids(loaded.state),pending)^
        validate(loaded.state,k.space)^
        dependencies(tx,k,loaded.state.endpoint,maintenance.get(loaded.state).revisions,"workspace",reach.cache_ids(loaded.state))^
        for (record in document_state.all(loaded.state)^) {
            documents.dependencies(tx,k,record,reach.cache_ids(loaded.state))^
            let exported = repo.get(tx,k,record.export_id,"document")^
            if (exported != document_state.public_state(record)^) raise error("sync local: document root mismatch")
        }
        if (loaded.state.previous != null) { repo.record(k,loaded.state.previous)^ }
        let snapshots = [for (id in loaded.state.snapshots) [id]]
        var i = 0
        while (i < len(snapshots)) {
            let current = loaded.state
            let saved = repo.get(tx,k,snapshots[i][0],"snapshot")^
            v.fields(saved,["version","label","workspace"])^
            if (saved.version != 1) raise error("sync local: unsupported snapshot")
            replica_dependencies(tx,k,c.restore(saved.workspace)^,[],0,maintenance.get(current).revisions,"snapshot",reach.cache_ids(current))^
            i = i+1
        }
        return checked
    })^
}

pub pn rebuild_index(handle) any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let loaded = repo.load(tx,k)^
        let found = [for (entry in repo.recover(tx,k)^) entry.id]
        let rebuilt = repo.rebuild_index(tx,k)^
        if (len(found) > 0) {
            let current = loaded.state
            repo.commit_head(tx,k,loaded.generation,{*:current,previous:loaded.id,
                recovery_pins:unique([*if (current.recovery_pins == null) [] else current.recovery_pins,*found])})^
        }
        repo.verify(tx,k,reach.cache_ids(repo.load(tx,k)^.state))^
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
                let candidate = documents.expand(tx,k,repo.get(tx,k,item.id,"state")^)^
                validate(candidate,k.space)^
                dependencies(tx,k,candidate.endpoint,maintenance.get(candidate).revisions,"workspace",reach.cache_ids(candidate))^
                for (record in document_state.all(candidate)^) { documents.dependencies(tx,k,record,reach.cache_ids(candidate))^ }
                candidates = [*candidates,item.id]
            }
            i = i+1
        }
        return {objects:found,candidates:candidates,advanced_head:false}
    })^
}

// only the cloud adapter calls this after verifying a complete discoverable publication.
pub pn record_cloud(handle, provider, actor, sequence, publication, captured, objects=[], document_roots=[]) any^ {
    if (not v.actor(provider)^ or not v.actor(actor)^ or not v.natural(sequence)^ or sequence < 1 or
        not v.actor(publication)^) raise error("sync local: invalid cloud progress")
    return mutate(handle,pn (tx,k,current)=> {
        let replica = c.restore(captured)^
        replica_dependencies(tx,k,replica,[],0,maintenance.get(current).revisions,"cloud receipt",reach.cache_ids(current))^
        let available = [*[for (item in blob.required(replica)^) item.manifest],*blob.checkpoints(replica)^]
        let endpoint = p.record_durable(current.endpoint,'cloud',captured,available)^
        let old = if (current.cloud_sources == null) [] else current.cloud_sources
        let previous = [for (item in old where item.provider == provider and item.actor == actor) item]
        if (len(previous) != 0 and (previous[0].sequence > sequence or
            (previous[0].sequence == sequence and previous[0].publication != publication)))
            raise error("sync local: cloud publication rollback/fork")
        let old_objects = if (current.cloud_objects == null) [] else current.cloud_objects
        let known = [for (item in old_objects where item.provider == provider) item]
        let next = {*:current,endpoint:endpoint,
            cloud_objects:[*[for (item in old_objects where item.provider != provider) item],
                {provider:provider,ids:unique([*objects,*if (len(known) == 0) [] else known[0].ids])}],cloud_sources:[*[for (item in old where
            item.provider != provider or item.actor != actor) item],
            {provider:provider,actor:actor,sequence:sequence,publication:publication,clock:replica.clock,documents:document_roots}]}
        return documents.record_cloud(tx,k,next,document_roots,provider)^
    })^
}

pub pn register_documents(handle, roots) any^ {
    return mutate(handle,pn (tx,k,current)=> { return documents.install_roots(tx,k,current,roots)^ })^
}

pub pn register_snapshots(handle, ids) any^ {
    return mutate(handle,pn (tx,k,current)=> {
        let admitted = [for (id in ids where not contains(maintenance.get(current).snapshots,id)^) id]
        for (id in admitted) {
            let saved = repo.get(tx,k,id,"snapshot")^
            replica_dependencies(tx,k,c.restore(saved.workspace)^,[],0,maintenance.get(current).revisions,"snapshot",reach.cache_ids(current))^
        }
        return {*:current,snapshots:unique([*current.snapshots,*admitted])}
    })^
}

pub pn record_cloud_control(handle, provider, generation, digest) any^ {
    if (not v.actor(provider)^ or not v.actor(digest)^ or not v.natural(generation)^ or generation < 1)
        raise error("sync local: invalid cloud control")
    return mutate(handle,pn (tx,k,current)=> {
        let old = if (current.cloud_controls == null) [] else current.cloud_controls
        let seen = [for (item in old where item.provider == provider) item]
        if (len(seen) > 0 and (generation < seen[0].generation or
            (generation == seen[0].generation and digest != seen[0].digest)))
            raise error("sync local: cloud control rollback/fork")
        return {*:current,cloud_controls:[*[for (item in old where item.provider != provider) item],
            {provider:provider,generation:generation,digest:digest}]}
    })^
}

// paired full-space checkpoint authorities can relay authenticated retirement manifests.
pub pn register_retention(handle, id) any^ {
    if (id == null) { return true }
    mutate(handle,pn (tx,k,current)=> {
        let value = repo.get(tx,k,id,"retention")^
        v.record(value,["version","generation","policy","overrides","pins","versions","revisions","at","roots"],["snapshots"])^
        let old = maintenance.get(current)
        let expired_snapshots = if (value.snapshots == null) [] else value.snapshots
        if (value.version != 1 or not v.natural(value.generation)^ or value.generation < 1)
            raise error("sync maintenance: invalid retirement manifest")
        if (old.manifest == id) { return {*:current,retention:{*:old,manifest_generation:value.generation}} }
        if (old.manifest != null) {
            let previous = repo.get(tx,k,old.manifest,"retention")^
            if (value.generation < previous.generation) { return current }
            if (value.generation == previous.generation) raise error("sync maintenance: retirement generation fork")
            if (not all([for (id in previous.versions) contains(value.versions,id)^]) or
                not all([for (id in previous.revisions) contains(value.revisions,id)^]) or
                not all([for (id in (if (previous.snapshots == null) [] else previous.snapshots)) contains(expired_snapshots,id)^]))
                raise error("sync maintenance: retirement cannot resurrect expired payloads")
        }
        return {*:current,retention:{*:old,generation:value.generation,policy:value.policy,
            overrides:value.overrides,pins:value.pins,versions:value.versions,revisions:value.revisions,
            snapshots:expired_snapshots,manifest:id,manifest_generation:value.generation},maintenance:null,
            snapshots:[for (snapshot in current.snapshots where not contains(expired_snapshots,snapshot)^) snapshot],
            recovery_pins:unique([*if (current.recovery_pins == null) [] else current.recovery_pins,
                *[for (snapshot in current.snapshots where contains(expired_snapshots,snapshot)^) snapshot]])}
    })^
    return true
}

pub pn release_staging(handle, ids) any^ {
    return mutate(handle,pn (tx,k,current)=> {
        return {*:current,staged_objects:[for (id in (if (current.staged_objects == null) [] else current.staged_objects)
            where not contains(ids,id)^) id]}
    })^
}

pub pn register_cache(handle, id) any^ {
    if (id == null) { return true }
    mutate(handle,pn (tx,k,current)=> { return cache_state.install(tx,k,current,id)^ })^
    return true
}
pub pn shared_inventory(handle) any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let current = repo.load(tx,k)^.state
        return if (maintenance.get(current).manifest == null and current.cache_root == null)
            [for (row in sqlite.query(tx,"SELECT id FROM objects WHERE kind NOT IN ('state', 'maintenance') ORDER BY id")^) row[0]]
            else reach.roots(tx,k,current,false)^
    })^
}

pub pn record_catalog_backup(handle, provider, id) any^ {
    if (id == null) { return true }
    return mutate(handle,pn (tx,k,current)=> {
        let old = if (current.cloud_catalogs == null) [] else current.cloud_catalogs
        return {*:current,cloud_catalogs:[*[for (entry in old where entry.id != provider) entry],{id:provider,snapshot:id}]}
    })^
}
