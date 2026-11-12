// ancestry links preserve discovery; only active/retained endpoints pin their physical payloads.
import repo: .repository
import history: .history
import model: .document_state
import retention_state: .maintenance_state
import blob: .blob
import core: .core
import v: .schema
import cache_state: .cache_state
import locations: .location_index

pub let cache_ids = cache_state.ids

fn manifests(replica, retired) any^ => [for (reference in retention_state.contents(replica,retired)^) reference.manifest]
fn checkpoints(keys, replica, retired) any^ => [for (digest in retention_state.checkpoints(replica,retired)^)
    repo.object_id(keys,"checkpoint",digest)^]
pub fn replica_roots(keys, replica, retired=[]) any^ => [*manifests(replica,retired)^,*checkpoints(keys,replica,retired)^]

pub pn closure(tx, keys, roots, retention, evicted=[], depth=0) any^ {
    if (depth > 128) raise error("sync maintenance: recovery-root depth exceeded")
    var pending = unique(roots)
    var allowed_remote = evicted
    var seen = []
    while (len(pending)^ > 0) {
        let id = pending[0]
        pending = slice(pending,1)^
        if (contains(seen,id)^) { continue }
        if (contains(allowed_remote,id)^ and not repo.present(keys,id)^) {
            seen = [*seen,id]
            continue
        }
        if (len(seen)^ >= 100000) raise error("sync maintenance: reachability budget exceeded")
        let record = repo.record(keys,id)^
        let value = repo.get(tx,keys,id,record.kind)^
        var children = []
        if (record.kind == "manifest" or record.kind == "file_page") {
            children = repo.content_links(record.kind,value)^
        } else if (record.kind == "checkpoint") {
            core.validate(value)^
            children = replica_roots(keys,value,retention.revisions)^
        } else if (record.kind == "snapshot") {
            // a named snapshot protects the visible revision at its own frontier.
            let replica = core.restore(value.workspace)^
            let protected = retention_state.live_revisions(replica)^
            children = replica_roots(keys,replica,[for (revision in retention_state.revisions(replica)^
                where not contains(protected,revision.id)^) revision.id])^
        } else if (record.kind == "version") {
            children = replica_roots(keys,value.replica)^
        } else if (record.kind == "history") {
            children = [*value.previous,*[for (header in value.entries where
                not contains(retention.versions,header.id)^) header.object]]
        } else if (record.kind == "document") {
            children = [*if (value.history == null) [] else value.history,
                *replica_roots(keys,value.live.replica)^,*replica_roots(keys,value.origin.replica)^,
                *if (value.versions == null) [] else [for (saved in value.versions)
                    for (id in replica_roots(keys,saved.replica)^) id]]
        } else if (record.kind == "maintenance") {
            children = value.payload.ids
        } else if (record.kind == "locator") {
            children = locations.links(keys,value)^
        } else if (record.kind == "cache") {
            children = []
        } else if (record.kind == "retention") {
            children = if (value.roots == null) [] else value.roots
        } else if (record.kind == "state") {
            // recovery candidates retain their own complete dependency contract until explicit review.
            let remote = cache_ids(value)
            allowed_remote = unique([*allowed_remote,*remote])
            children = closure(tx,keys,state_roots(tx,keys,value,true)^,retention_state.get(value),remote,depth+1)^
        } else if (not repo.is_chunk(record.kind)^) {
            raise error("sync maintenance: unsupported reachable object kind")
        }
        seen = [*seen,id]
        pending = [*pending,*children]
    }
    return sort(seen)^
}
pub fn staged(current) => if (current.staged_objects == null) [] else current.staged_objects
pn state_roots(tx, keys, current, include_private=true) any^ {
    let retention = retention_state.get(current)
    var roots = [*replica_roots(keys,current.endpoint.replica,retention.revisions)^,*current.snapshots,
        *[for (record in model.all(current)) record.export_id]]
    if (retention.manifest != null) { roots = [*roots,retention.manifest] }
    if (current.cache_root != null) { roots = [*roots,current.cache_root] }
    if (include_private) {
        roots = [*roots,*[for (entry in locations.roots(current) where entry.root != null) locations.physical_id(keys,entry.root)^]]
        roots = [*roots,*staged(current),*if (current.recovery_pins == null) [] else current.recovery_pins]
        // disk/import/installation bases stay local pins even when shared history expires.
        roots = [*roots,*replica_roots(keys,current.base.checkpoint.replica,retention.revisions)^,
            *[for (member in current.disk where member.type == 'file') member.content.manifest]]
        for (record in model.all(current)) {
            roots = [*roots,*replica_roots(keys,record.session.base.replica)^,
                *if (record.prepared == null) [] else replica_roots(keys,record.prepared.saved.replica)^]
            for (conflict in record.session.conflicts) {
                roots = [*roots,*replica_roots(keys,conflict.base.replica)^,
                    *replica_roots(keys,conflict.live_before)^,
                    *if (conflict.branch == null) [] else replica_roots(keys,conflict.branch)^]
            }
        }
        if (current.installation != null or current.review != null)
            raise error("sync maintenance: finish installation/review before planning cleanup")
    }
    return unique(roots)
}
pub pn roots(tx, keys, current, include_private=true) any^ {
    let roots = state_roots(tx,keys,current,include_private)^
    let orphans = if (include_private) [for (entry in repo.recover(tx,keys)^) entry.id] else []
    return closure(tx,keys,[*roots,*orphans],retention_state.get(current),cache_ids(current))^
}
pub pn hot(tx, keys, current) any^ {
    let retention = retention_state.get(current)
    var roots = [*current.snapshots,*staged(current),
        *if (current.recovery_pins == null) [] else current.recovery_pins,
        *[for (entry in repo.recover(tx,keys)^) entry.id],
        *[for (member in current.disk where member.type == 'file') member.content.manifest]]
    let revisions = retention_state.revisions(current.endpoint.replica)^
    let heads = retention_state.live_revisions(current.endpoint.replica)^
    for (revision in revisions where contains(heads,revision.id)^) {
        roots = [*roots,revision.content.manifest,*if (revision.document == null) [] else
            [repo.object_id(keys,"checkpoint",revision.document.checkpoint)^]]
    }
    for (record in model.all(current)) {
        roots = [*roots,*replica_roots(keys,record.endpoint.replica)^,*replica_roots(keys,record.origin.replica)^,
            *replica_roots(keys,record.session.base.replica)^,
            *if (record.prepared == null) [] else replica_roots(keys,record.prepared.saved.replica)^]
        for (header in history.headers(tx,keys,history.roots(record))^.entries where contains(retention.pins,header.id)^) {
            roots = [*roots,header.object]
        }
        for (conflict in record.session.conflicts) {
            roots = [*roots,*replica_roots(keys,conflict.base.replica)^,*replica_roots(keys,conflict.live_before)^,
                *if (conflict.branch == null) [] else replica_roots(keys,conflict.branch)^]
        }
    }
    if (current.installation != null or current.review != null)
        raise error("sync cache: finish installation/review before eviction")
    return closure(tx,keys,roots,retention,cache_ids(current))^
}
