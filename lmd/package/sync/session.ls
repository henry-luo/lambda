// pure session/history model: no filesystem, transport or implicit save effects.
import c: .core
import text: .text
import mark: .mark

pub fn value(replica) any^ => if (replica.profile == 'text') text.read(replica)^ else mark.value(replica)^

fn encoded(replica) any^ => if (replica.profile == 'text') text.read(replica)^ else format(mark.value(replica)^,'mark')^

fn version(replica, raw, projected, parents) any^ {
    let saved = c.snapshot(replica)^
    return {*:saved, id:hash(format([saved.id,raw],'mark')^,'sha256')^,
        raw:raw, value:projected, parents:parents}
}

fn remember(versions, saved) =>
    if (any([for (item in versions) item.id == saved.id])) versions else [*versions,saved]

pub fn start(replica) any^ {
    let initial = version(replica,encoded(replica)^,value(replica)^,[])^
    return {live:replica,base:initial,disk:initial.value,versions:[initial],imports:[],notices:[],conflicts:[],deleted:false}
}

pub fn update(session, replica) any^ => {*:session,live:c.merge(session.live,replica)^}

pub fn dirty(session) any^ {
    if (len(session.conflicts) > 0 or session.deleted) { return true }
    let projected: any^ = value(session.live)
    if (projected is error) { return true }
    return projected != session.disk
}

fn same_deleted_targets(base, a, b) any^ => any([for (atom in base.atoms where
    not contains(base.dead,atom.id)^) contains(a.dead,atom.id)^ and contains(b.dead,atom.id)^])

fn changed_node(base, replica, id) any^ {
    let before: any^ = mark.value_at(base,id)
    let after: any^ = mark.value_at(replica,id)
    if (before is error or after is error) { return true }
    return before != after
}

fn removed_edited_child(base, a, b) any^ {
    let original = mark.reachable(base)^
    let visible = mark.reachable(b)^
    return any([for (id in original where not contains(visible,id)^) changed_node(base,a,id)^])
}

fn overlap(base, a, b, merged) any^ {
    if (same_deleted_targets(base,a,b)^) { return true }
    if (base.profile == 'text') { return false }
    return len(mark.conflicts(merged)^) > 0 or removed_edited_child(base,a,b)^ or removed_edited_child(base,b,a)^
}

fn hold(session, record, reason) => {*:session,
    imports:[*session.imports,{*:record,status:'review',reason:reason}],
    conflicts:[*session.conflicts,{kind:'external-review',import_id:record.id,reason:reason,
        base:session.base,live_before:session.live,raw:record.raw}],
    notices:[*session.notices,"External changes need review; your unsaved edits are retained."]}

// the host supplies captured raw bytes and either parsed Mark data or a parse error.
pub fn import_external(session, import_id, actor, base_id, raw, parsed:any) any^ {
    if (not (import_id is string) or import_id == "") raise error("sync: import ID must be nonempty text")
    let projected = if (parsed is error) {parse_error:parsed.message} else parsed
    let fingerprint = hash(format([actor,base_id,raw,projected],'mark')^,'sha256')^
    let previous = c.lookup(session.imports,import_id)^
    if (previous != null) {
        if (previous.fingerprint != fingerprint) raise error("sync: import ID reused with different input")
        return session
    }
    let record = {id:import_id,actor:actor,base_id:base_id,raw:raw,external:projected,fingerprint:fingerprint}
    if (base_id != session.base.id) { return hold(session,record,"unknown or stale external base") }
    if (parsed is error) { return hold(session,record,parsed.message) }
    if (session.deleted) { return hold(session,record,"document was deleted") }
    if (not (actor is string) or actor == "" or c.clock_get(session.live.clock,actor) != 0) {
        return hold(session,record,"external import needs a fresh document actor")
    }
    // fork from the disk base, not from the local actor's already edited state.
    let branch: any^ = if (session.live.profile == 'text') text.reconcile(session.base.replica,actor,parsed)
        else mark.reconcile(session.base.replica,actor,parsed)
    if (branch is error) { return hold(session,record,branch.message) }
    let merged = c.merge(session.live,branch)^
    let conflict = overlap(session.base.replica,session.live,branch,merged)^
    let unsaved = dirty(session)^
    let external_value = if (session.live.profile == 'text') parsed else mark.normalize(parsed)^
    let saved = version(branch,raw,external_value,[session.base.id])^
    let imported = {*:record,status:if (conflict) 'conflict' else 'merged',branch:branch,version:saved.id}
    return {*:session,live:merged,base:saved,disk:external_value,
        versions:remember(session.versions,saved),imports:[*session.imports,imported],
        conflicts:if (conflict) [*session.conflicts,{kind:'external-overlap',import_id:import_id,
            base:session.base,live_before:session.live,branch:branch,raw:raw}] else session.conflicts,
        notices:if (conflict) [*session.notices,"Concurrent external edits need review; both inputs are retained."]
            else if (unsaved) [*session.notices,"External changes merged; your unsaved edits are retained."] else session.notices}
}

pub fn save(session) any^ {
    if (session.deleted or len(session.conflicts) > 0) raise error("sync: resolve external conflicts before saving")
    let saved = version(session.live,encoded(session.live)^,value(session.live)^,[session.base.id])^
    return {*:session,base:saved,disk:saved.value,versions:remember(session.versions,saved)}
}

pub fn resolve(session, replica) any^ {
    if (not c.dominates(replica.clock,session.live.clock)^) raise error("sync: resolution must preserve observed causal context")
    let merged = c.merge(session.live,replica)^
    value(merged)^
    return {*:session,live:merged,conflicts:[],imports:[for (record in session.imports)
        if (record.status == 'review' or record.status == 'conflict') {*:record,status:'resolved'} else record]}
}

pub fn deleted(session) => {*:session,deleted:true,
    notices:[*session.notices,"File deleted outside Lambda; unsaved edits remain recoverable."]}

pub fn compact(session) => {*:session,live:c.compact(session.live),imports:[for (record in session.imports)
    if (record.status == 'merged' or record.status == 'resolved' or record.status == 'compacted')
        {id:record.id,actor:record.actor,base_id:record.base_id,fingerprint:record.fingerprint,status:'compacted'}
    else record]}

// models logical history selection, not object-store deletion or calendar policy.
pub fn retain(session, ids) any^ {
    let protected = [session.base.id,*ids,*[for (conflict in session.conflicts) conflict.base.id]]
    return {*:session,versions:[for (saved in session.versions where contains(protected,saved.id)^) saved]}
}
