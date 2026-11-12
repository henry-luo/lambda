// shared captured-workspace reconciliation; ZIP v1 identity domains remain stable.
import c: .core
import v: .schema
import codec: .codec
import p: .protocol
import w: .workspace
import tree: .workspace_tree
import command_model: .workspace_commands

pub fn leaf(path) any^ {
    let pieces = split(path,"/")^
    return pieces[len(pieces)-1]
}
pub fn parent_path(path) any^ {
    let pieces = split(path,"/")^
    return join(take(pieces,len(pieces)-1)^,"/")^
}

pub fn capture_id(members) any^ => codec.digest(["lambda-zip-capture-1",[for (member in members)
    {path:member.path,type:member.type,size:member.content.size,hash:member.content.hash}]])^

pub fn apply_groups(endpoint, actor, commands, max_ops=128, max_bytes=65536) any^ {
    var result = endpoint
    var i = 0
    while (i < len(commands)) {
        var count = min(max_ops,len(commands)-i)
        var edited = w.transact(result,actor,slice(commands,i,i+count)^)^.endpoint
        var packet = p.transaction(result.replica,edited.replica,w.model)^
        // bound the actual wire image, including long member names and context.
        while (len(binary(codec.encode(packet)^))^ > max_bytes and count > 1) {
            count = count div 2
            edited = w.transact(result,actor,slice(commands,i,i+count)^)^.endpoint
            packet = p.transaction(result.replica,edited.replica,w.model)^
        }
        if (len(binary(codec.encode(packet)^))^ > max_bytes) raise error("sync ZIP: event exceeds transfer budget")
        result = edited
        i = i+count
    }
    return result
}

pub fn baseline(replica, members, capture_id) any^ {
    let checkpoint = c.snapshot(replica)^
    // internal records use a fixed field order; user Mark maps retain S2.3.1v2.
    let normalized = [for (member in members) {id:member.id,path:member.path,type:member.type,content:member.content}]
    let body = {checkpoint:checkpoint,members:normalized,capture:capture_id}
    return {id:codec.digest(["lambda-zip-base-1",body])^,*:body}
}

pub fn association(members, path) any^ {
    let matches = [for (member in members where member.path == path) member]
    return if (len(matches) == 0) null else matches[0]
}

pub fn desired_path(member, members, hints) any^ {
    let candidates = [for (hint in hints) {original:v.lookup(members,hint.entry)^,path:hint.path}]
    let matches = sort([for (hint in candidates where member.id == hint.original.id or
        (hint.original.type == 'dir' and starts_with(member.path,hint.original.path ++ "/")^))
        {from:hint.original.path,to:hint.path}],(hint) any^ =>len(hint.from)^)^
    if (len(matches) == 0) { return member.path }
    let chosen = matches[len(matches)-1]
    return chosen.to ++ slice(member.path,len(chosen.from),len(member.path))^
}

pub fn correspondence(base, captured, hints) any^ {
    if (not (hints is array)) raise error("sync ZIP: invalid rename associations")
    var i = 0
    while (i < len(hints)) {
        let hint = hints[i]
        v.fields(hint,["entry","path"])^
        let old = v.lookup(base.members,hint.entry)^
        let target = association(captured.members,hint.path)^
        if (old == null or target == null or old.type != target.type) raise error("sync ZIP: invalid rename association")
        i = i+1
    }
    if (len(unique([for (hint in hints) hint.entry])) != len(hints)) raise error("sync ZIP: ambiguous rename association")
    let result = [for (member in base.members) {*:member,path:desired_path(member,base.members,hints)^}]
    if (len(unique([for (member in result) member.path])) != len(result)) raise error("sync ZIP: ambiguous member identity")
    return result
}

// each command emits one event, so IDs can be assigned before bounded groups run.
pub fn import_branch(endpoint, actor, captured, old_members=[], documents=[], saved_at=null) any^ {
    let base_view = w.read(endpoint.replica)^
    var members = []
    var commands = []
    var i = 0
    while (i < len(captured.members)^) {
        let member = captured.members[i]
        let old = association(old_members,member.path)^
        let parent = parent_path(member.path)^
        let owner = if (parent == "") w.root else association(members,parent)^.id
        if (owner == null) raise error("sync ZIP: parent identity missing")
        var id = if (old == null) [actor,c.clock_get(endpoint.replica.clock,actor)+len(commands)+1] else old.id
        var reference = member.content
        if (old == null) {
            commands = [*commands,{kind:'create',type:member.type,parent:owner,name:leaf(member.path)^,content:member.content}]
        } else {
            if (old.type != member.type) raise error("sync ZIP: member type changed; identity needs review")
            let previous = v.lookup(base_view.entries,id)^
            // a generated conflict filename is a view, not a logical rename.
            if (previous.parent != owner or leaf(old.original_path or old.path)^ != leaf(member.path)^) {
                commands = [*commands,{kind:'move',entry:id,parent:owner,name:leaf(member.path)^}]
            }
            let changed = member.type == 'file' and
                (old.content.hash != member.content.hash or old.content.size != member.content.size)
            let linked = v.lookup(documents,id)^
            if (changed or linked != null) {
                commands = [*commands,{kind:'publish',entry:id,content:member.content,document:linked.document}]
            } else if (member.type == 'file') {
                reference = old.content
            }
        }
        members = [*members,{*:member,id:id,content:reference}]
        i = i+1
    }
    let removed = [for (old in old_members where not any([for (member in members) member.id == old.id])) old]
    let placements = tree.placements(tree.events(endpoint.replica)^)^.entries
    i = 0
    while (i < len(removed)) {
        let old = removed[i]
        if (not any([for (ancestor in removed where ancestor.id != old.id) tree.ancestor(placements,old.id,ancestor.id)^])) {
            commands = [*commands,{kind:'delete',entry:old.id}]
        }
        i = i+1
    }
    return {endpoint:apply_groups(endpoint,actor,command_model.stamp(commands,saved_at)^)^,members:members}
}

pub fn export_members(entries) any^ => sort([for (entry in entries where entry.visible)
    {id:entry.id,path:tree.path(entries,entry.id,true)^,type:entry.type,
     content:if (entry.type == 'file') entry.heads[0].content else null}],(entry)=>entry.path)^

pub fn validate_base(base) any^ {
    v.fields(base,["id","checkpoint","members","capture"])^
    let replica = c.restore(base.checkpoint)^
    if (replica.profile != 'workspace') raise error("sync ZIP: baseline requires a workspace")
    let entries = w.read(replica)^.entries
    if (any([for (entry in entries where entry.visible and entry.type == 'file') len(entry.heads)^ != 1]))
        raise error("sync ZIP: baseline has unresolved revisions")
    let members = export_members(entries)^
    if (members != base.members or base.capture != capture_id(members)^ or
        base.id != baseline(replica,members,base.capture)^.id) raise error("sync ZIP: invalid baseline")
    return replica
}
