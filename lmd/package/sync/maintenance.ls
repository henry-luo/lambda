// key-holding host operations; plans bind policy and all local roots before any destructive effect.
import local: .local
import repo: .repository
import retention_state: .maintenance_state
import selection: .retention
import reach: .reachability
import history: .history
import model: .document_state
import documents: .documents
import editor: .document_host
import workspace: .workspace
import core: .core
import codec: .codec
import v: .schema


fn linked(header, revision) bool^ => revision.document != null and revision.document.checkpoint == header.checkpoint and
    revision.content.hash == header.raw_hash
fn representative(headers, revision, retired=[]) any^ {
    let matching = [for (header in headers where not contains(retired,header.id)^ and linked(header,revision)^) header]
    let latest = selection.extremes(headers,matching)^
    let ordered = sort(latest,(header)=>[header.saved_at,header.id])^
    return if (len(ordered) == 0) [] else [ordered[len(ordered)-1].id]
}

pub let fingerprint = retention_state.fingerprint
pub pn headers(tx, keys, current) any^ {
    var saved = []
    for (record in model.all(current)^) { saved = [*saved,*history.headers(tx,keys,history.roots(record))^.entries] }
    let retention = retention_state.get(current)
    let versions = [for (header in saved) {*:header,arrived_at:v.lookup(retention.arrivals,header.id)^.at}]
    let files = [for (revision in retention_state.revisions(current.endpoint.replica)^) {
        let matching = [for (header in saved where revision.document != null and
            revision.document.checkpoint == header.checkpoint and revision.content.hash == header.raw_hash) header]
        {id:revision.id,group:model.id(keys.space,revision.file)^,parents:revision.parents,
            saved_at:if (len(matching)^ > 0) matching[0].saved_at else v.lookup(retention.arrivals,revision.id)^.at,
            arrived_at:v.lookup(retention.arrivals,revision.id)^.at,revision:revision}
    }]
    return {versions:versions,files:files}
}
pub pn protected(tx, keys, current, catalog) any^ {
    let retention = retention_state.get(current)
    var revisions = retention_state.live_revisions(current.endpoint.replica)^
    for (id in current.snapshots) {
        revisions = [*revisions,*retention_state.live_revisions(core.restore(repo.get(tx,keys,id,"snapshot")^.workspace)^)^]
    }
    var versions = []
    for (record in model.all(current)^) {
        versions = [*versions,record.origin.id,record.session.base.id,
            *if (record.prepared == null) [] else [record.prepared.saved.id],
            *[for (conflict in record.session.conflicts) conflict.base.id]]
    }
    versions = [*versions,*[for (revision_header in catalog.files where contains(revisions,revision_header.id)^)
        for (id in representative(catalog.versions,revision_header.revision,retention.versions)^) id]]
    return unique([*revisions,*versions,*retention.pins])
}
pub pn configure(handle, policy, overrides=[]) any^ {
    selection.validate(policy)^
    if (not (overrides is array) or len(unique([for (entry in overrides) entry.id])) != len(overrides)^)
        raise error("sync maintenance: invalid overrides")
    for (entry in overrides) { selection.validate(entry.policy)^ }
    return local.mutate(handle,pn (tx,k,current)=> {
        let old = retention_state.get(current)
        return {*:current,maintenance:null,retention:{*:old,generation:old.generation+1,
            policy:policy,overrides:overrides,pending:[]}}
    })^
}
pub pn pin(handle, ids, enabled=true) any^ {
    return local.mutate(handle,pn (tx,k,current)=> {
        let old = retention_state.get(current)
        if (enabled and any([for (id in ids) contains([*old.versions,*old.revisions],id)^]))
            raise error("sync maintenance: retired history must be explicitly restored")
        return {*:current,maintenance:null,retention:{*:old,generation:old.generation+1,
            pins:if (enabled) sort(unique([*old.pins,*ids]))^ else [for (id in old.pins where not contains(ids,id)^) id]}}
    })^
}
pub pn release_snapshot(handle, id) any^ {
    return local.mutate(handle,pn (tx,k,current)=> {
        let old = retention_state.get(current)
        return {*:current,snapshots:[for (snapshot in current.snapshots where snapshot != id) snapshot],
            maintenance:null,retention:{*:old,generation:old.generation+1,
                snapshots:sort(unique([*old.snapshots,id]))^,pending:[]}}
    })^
}
pub pn plan(handle, milliseconds) any^ {
    // migrate old packed roots and first-observation timestamps before binding the deletion plan.
    local.mutate(handle,pn (tx,k,current)=> { return current })^
    let planned = local.mutate(handle,pn (tx,k,current)=> {
        if (current.installation != null or current.review != null)
            raise error("sync maintenance: finish installation/review before retention")
        let catalog = headers(tx,k,current)^
        let retention = retention_state.get(current)
        let protected = protected(tx,k,current,catalog)^
        let all_headers = [*catalog.versions,*catalog.files]
        var expired = []
        for (group in unique([for (header in all_headers) header.group])) {
            let policy = retention_state.selected_policy(retention,group)^
            let selected = selection.select([for (header in all_headers where header.group == group) header],policy,milliseconds,protected)^
            expired = [*expired,*selected.candidates]
        }
        // retired ancestry participates in selection but never becomes a fresh deletion candidate.
        let already_retired = [*retention.versions,*retention.revisions]
        expired = unique([*expired,*already_retired])
        // linked file and editing checkpoints are retained together.
        let linked_keep = [for (revision_header in catalog.files where not contains(expired,revision_header.id)^)
            for (id in representative(catalog.versions,revision_header.revision,retention.versions)^) id]
        let linked_files = [for (revision_header in catalog.files where revision_header.revision.document != null and any([
            for (version in catalog.versions) not contains(expired,version.id)^ and linked(version,revision_header.revision)^])) revision_header.id]
        expired = [for (id in expired where not contains([*linked_keep,*linked_files,*already_retired],id)^) id]
        let pending = [for (id in expired) {id:id,at:if (v.lookup(retention.pending,id)^ == null)
            milliseconds else v.lookup(retention.pending,id)^.at}]
        let next = {*:current,retention:{*:retention,pending:pending}}
        let binding = fingerprint(next)^
        let intent = {version:1,kind:'forget',id:codec.digest([binding,expired,milliseconds])^,
            fingerprint:binding,at:milliseconds,versions:[for (header in catalog.versions where contains(expired,header.id)^) header.id],
            revisions:[for (header in catalog.files where contains(expired,header.id)^) header.id],protected:protected}
        return {*:next,maintenance:intent}
    })^
    return planned.state.maintenance
}
fn check(current, id, kind) any^ {
    let plan = current.maintenance
    if (plan == null or plan.id != id or plan.kind != kind or plan.fingerprint != fingerprint(current)^)
        raise error("sync maintenance: stale or wrong plan; replan required")
    return plan
}
fn eligible(current, plan, catalog, milliseconds) bool^ {
    let old = retention_state.get(current)
    return milliseconds >= plan.at and all([for (target in [*plan.versions,*plan.revisions]) {
        let header = v.lookup([*catalog.versions,*catalog.files],target)^
        milliseconds-v.lookup(old.pending,target)^.at >= retention_state.selected_policy(old,header.group)^.delete_grace_ms
    }])
}
pub pn ready(handle, id, milliseconds) any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let current = documents.expand(tx,k,repo.load(tx,k)^.state)^
        let plan = check(current,id,'forget')^
        return eligible(current,plan,headers(tx,k,current)^,milliseconds)^
    })^
}
pub pn forget(handle, id, milliseconds) any^ {
    return local.mutate(handle,pn (tx,k,current)=> {
        let plan = check(current,id,'forget')^
        let old = retention_state.get(current)
        let catalog = headers(tx,k,current)^
        if (not eligible(current,plan,catalog,milliseconds)^) raise error("sync maintenance: deletion grace has not elapsed")
        let next = {*:old,generation:old.generation+1,versions:sort(unique([*old.versions,*plan.versions]))^,
            revisions:sort(unique([*old.revisions,*plan.revisions]))^,pending:[]}
        let manifest = {version:1,generation:next.generation,policy:next.policy,overrides:next.overrides,
            pins:next.pins,versions:next.versions,revisions:next.revisions,snapshots:next.snapshots,at:milliseconds,roots:[]}
        let stored = repo.store_object(tx,k,"retention",manifest)^
        var updated = {*:current,retention:{*:next,manifest:stored,manifest_generation:next.generation},maintenance:null}
        for (record in model.all(updated)) {
            let compacted = model.compact(record)^
            updated = model.replace(updated,{*:compacted,session:{*:compacted.session,
                versions:[for (saved in compacted.session.versions where not contains(next.versions,saved.id)^) saved]}})^
        }
        return updated
    })^
}
pub pn plan_prune(handle) any^ {
    let result = local.mutate(handle,pn (tx,k,current)=> {
        let loaded = repo.load(tx,k)^
        let reachable = [*reach.roots(tx,k,current)^,loaded.id,*if (current.previous == null) [] else [current.previous]]
        let candidates = [for (id in repo.inventory(tx)^ where not contains(reachable,id)^) id]
        if (len(candidates)^ > 8192) raise error("sync maintenance: prune plan object budget exceeded")
        let binding = fingerprint(current)^
        return {*:current,maintenance:{version:1,kind:'prune',id:codec.digest([binding,candidates])^,
            fingerprint:binding,objects:candidates,next:0}}
    })^
    return result.state.maintenance
}
pub pn prune_step(handle, id, budget=16) any^ {
    if (not v.natural(budget)^ or budget < 1 or budget > 128) raise error("sync maintenance: invalid cleanup budget")
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let loaded = repo.load(tx,k)^
        let current = documents.expand(tx,k,loaded.state)^
        let plan = check(current,id,'prune')^
        let reachable = [*reach.roots(tx,k,current)^,loaded.id,*if (current.previous == null) [] else [current.previous]]
        let end = min(len(plan.objects)^,plan.next+budget)
        let next = {*:loaded.state,maintenance:{*:plan,next:end},previous:loaded.id}
        // the uncommitted head write acquires SQLite's writer lock before any filesystem removal.
        repo.commit_head(tx,k,loaded.generation,next)^
        for (offset in range(plan.next,end,1)) {
            let target = plan.objects[offset]
            if (contains(reachable,target)^) raise error("sync maintenance: object became protected; replan required")
            repo.remove_object(tx,k,target)^
        }
        return {id:id,removed:end-plan.next,next:end,total:len(plan.objects)^,complete:end == len(plan.objects)^}
    })^
}

pub pn release_recovery(handle, ids) any^ {
    return local.mutate(handle,pn (tx,k,current)=> {
        return {*:current,recovery_pins:[for (id in (if (current.recovery_pins == null) [] else current.recovery_pins)
            where not contains(ids,id)^) id]}
    })^
}

pub pn file_history(handle, entry, cursor=0, limit=32, expected_root=null) any^ {
    if (not v.natural(cursor)^ or not v.natural(limit)^ or limit < 1 or limit > 128)
        raise error("sync history: invalid file page request")
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let current = repo.load(tx,k)^.state
        let retention = retention_state.get(current)
        let all_headers = headers(tx,k,current)^.files
        let versions = sort([for (header in all_headers where header.revision.file == entry) header],
            (header)=>[header.saved_at,header.id])^
        let root = codec.digest([entry,versions,retention.manifest])^
        if (expected_root != null and expected_root != root) raise error("sync history: file history changed; restart paging")
        let page = slice(versions,cursor,cursor+limit)^
        return {versions:[for (header in page) {*:header.revision,saved_at:header.saved_at,
            arrived_at:header.arrived_at,availability:if (contains(retention.revisions,header.id)^) 'retired' else 'retained'}],
            next:if (cursor+len(page)^ < len(versions)^) cursor+len(page)^ else null,root:root}
    })^
}
pub pn restore_file(handle, entry, id) any^ {
    local.scan(handle)^
    let selected = repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let current = repo.load(tx,k)^.state
        let revision = v.lookup(retention_state.revisions(current.endpoint.replica)^,id)^
        if (revision == null or revision.file != entry or not workspace.entry(current.endpoint.replica,entry)^.visible)
            raise error("sync history: select a known visible file revision")
        if (contains(retention_state.get(current).revisions,id)^) raise error("sync history: file payload intentionally retired")
        if (revision.document != null) {
            let available = [for (record in model.all(current)^ where record.entry == entry) record]
            if (len(available) == 0) raise error("sync history: managed document state unavailable")
            let catalog = history.headers(tx,k,history.roots(available[0]))^.entries
            let matching = [for (header in catalog where linked(header,revision)^ and
                not contains(retention_state.get(current).versions,header.id)^) header]
            if (len(matching) == 0) raise error("sync history: saved editing endpoint unavailable")
            return {document:revision.document.document,version:matching[0].id}
        }
        let payload = repo.availability(tx,k,revision.content,reach.cache_ids(current))^
        if (not payload.complete) raise error("sync cache: hydrate retained history before restoring")
        return {document:null,content:revision.content}
    })^
    if (selected.document != null) {
        editor.restore_version(handle,selected.document,selected.version)^
        return editor.save(handle,selected.document)^
    }
    local.transact(handle,[{kind:'publish',entry:entry,content:selected.content}])^
    return local.materialize(handle)^
}
