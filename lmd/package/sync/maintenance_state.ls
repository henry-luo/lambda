// retention records change payload availability, never namespace events or causal identity.
import policy: .retention_policy
import tree: .workspace_tree
import workspace: .workspace
import blob: .blob
import v: .schema
import codec: .codec

pub fn get(current) => {
    let defaults = {version:1,generation:0,policy:policy.permanent,overrides:[],pins:[],versions:[],revisions:[],
        snapshots:[],checkpoints:[],pending:[],arrivals:[],manifest:null,manifest_generation:0};
    if (current.retention == null) defaults else {*:defaults,*:current.retention}
}
pub fn contents(replica, retired=[]) any^ => if (replica.profile != 'workspace') blob.required(replica)^
    else unique([for (event in tree.events(replica)^ where
        (event.action == 'publish' or (event.action == 'create' and event.type == 'file')) and
        not contains(retired,event.revision.id)^) event.revision.content])
pub fn checkpoints(replica, retired=[]) any^ => if (replica.profile != 'workspace') blob.checkpoints(replica)^
    else unique([for (event in tree.events(replica)^ where
        (event.action == 'publish' or (event.action == 'create' and event.type == 'file')) and
        not contains(retired,event.revision.id)^ and event.revision.document != null) event.revision.document.checkpoint])
pub fn revisions(replica) any^ => unique([for (event in tree.events(replica)^ where
    event.action == 'publish' or (event.action == 'create' and event.type == 'file')) event.revision])
pub fn live_revisions(replica) any^ => [for (entry in workspace.read(replica)^.entries)
    for (head in entry.heads) head.id]
pub fn observe(current, headers, milliseconds) any^ {
    let old = get(current)
    let ids = unique([*[for (header in headers) header.id],*[for (revision in revisions(current.endpoint.replica)^) revision.id]])
    return {*:current,retention:{*:old,arrivals:[*old.arrivals,
        *[for (id in ids where v.lookup(old.arrivals,id)^ == null) {id:id,at:milliseconds}]]}}
}
pub fn selected_policy(retention, group) any^ {
    let override = v.lookup(retention.overrides,group)^
    return if (override == null) retention.policy else override.policy
}

pub fn fingerprint(current) any^ => codec.digest({*:current,maintenance:null,previous:null})^
