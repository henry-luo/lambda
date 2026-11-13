// ordered move replay and namespace projection; retained events are the authority.
import v: .schema
import codec: .codec

pub fn seen(context, id) any^ => id == v.root or v.clock_get(context,id[0]) >= id[1]

pub fn events(replica) any^ => sort([for (atom in replica.atoms)
    {*:atom.value.event,id:atom.id,time:atom.time,context:v.lookup_id(replica.receipts,atom.id)^.context}],
    (event)=>[event.time,event.id])^

pub fn ancestor(entries, child, parent) any^ {
    var current = child
    var remaining = len(entries)+1
    while (current != null and remaining > 0) {
        if (current == parent) { return true }
        let entry = v.lookup(entries,current)^
        if (entry == null) { return false }
        current = entry.parent
        remaining = remaining-1
    }
    if (remaining == 0) raise error("sync workspace: invalid cyclic placement")
    return false
}

pub fn placements(events) any^ {
    var entries = [{id:v.root,type:'dir',parent:null,name:"",placement:null}]
    var suppressed = []
    var i = 0
    while (i < len(events)) {
        let event = events[i]
        if (event.action == 'create') {
            entries = [*entries,{id:event.id,type:event.type,parent:event.parent,name:event.name,placement:event.id}]
        } else if (event.action == 'place') {
            // full ordered replay is the reference form of suffix undo/replay.
            if (ancestor(entries,event.parent,event.entry)^) {
                suppressed = [*suppressed,{id:event.id,entry:event.entry,parent:event.parent,name:event.name}]
            } else {
                entries = [for (entry in entries) if (entry.id == event.entry)
                    {*:entry,parent:event.parent,name:event.name,placement:event.id} else entry]
            }
        }
        i = i+1
    }
    return {entries:entries,suppressed:suppressed}
}

fn active_deletions(events) any^ => [for (event in events where event.action == 'delete' and
    not any([for (other in events where other.action == 'restore' and other.entry == event.entry)
        contains(other.deletions,event.id)^])) event]
pub fn deletions(events, entry) any^ => [for (event in active_deletions(events)^ where event.entry == entry) event]

pub fn revisions(events, entry) any^ => sort(unique([for (event in events where
    (event.action == 'create' and event.id == entry and event.type == 'file') or
    (event.action == 'publish' and event.entry == entry)) event.revision]),(revision)=>revision.id)^

pub fn heads(events, entry) any^ {
    let versions = revisions(events,entry)^
    return [for (revision in versions where not any([for (other in versions) contains(other.parents,revision.id)^])) revision]
}

fn hidden_by(entries, deleted, id) any^ => any([for (event in deleted) ancestor(entries,id,event.entry)^])
pub fn hidden(entries, events, id) any^ => hidden_by(entries,active_deletions(events)^,id)^

fn alternatives(events, entry) any^ {
    let moves = [for (event in events where (event.action == 'create' and event.id == entry) or
        (event.action == 'place' and event.entry == entry)) event]
    return [for (event in moves where not any([for (other in moves where other.id != event.id)
        seen(other.context,event.id)^])) event]
}

fn names(entries) any^ {
    var reserved = [for (entry in entries) {parent:entry.parent,name:entry.name}]
    var result = []
    let ordered = sort([for (entry in entries where entry.id != v.root) entry],(entry)=>entry.id)^
    var i = 0
    while (i < len(ordered)) {
        let entry = ordered[i]
        var name = entry.name
        if (len([for (other in entries where other.parent == entry.parent and other.name == entry.name) other]) > 1) {
            name = entry.name ++ "~id~" ++ url_encode(codec.encode(entry.id)^)^
            // reserve every real name before assigning generated conflict names.
            while (any([for (other in reserved) other.parent == entry.parent and other.name == name])) { name = name ++ "~" }
        }
        reserved = [*reserved,{parent:entry.parent,name:name}]
        result = [*result,{id:entry.id,name:name}]
        i = i+1
    }
    return result
}

pub fn project(events) any^ {
    let replay = placements(events)^
    let entries = replay.entries
    // Deletion work depends on active deletions, not every entry in a live tree.
    let deleted = active_deletions(events)^
    let invisible = [for (entry in entries where hidden_by(entries,deleted,entry.id)^) entry.id]
    let visible_names = names([for (entry in entries where entry.id != v.root and not contains(invisible,entry.id)^) entry])^
    let rows = [for (entry in entries where entry.id != v.root)
        {*:entry,visible:not contains(invisible,entry.id)^,
         materialized_name:v.lookup(visible_names,entry.id)^.name or entry.name,
         deletions:[for (event in deleted where event.entry == entry.id) event.id],
         heads:if (entry.type == 'file') heads(events,entry.id)^ else []}]
    var conflicts = [for (move in replay.suppressed) {kind:'cycle',*:move}]
    let deleted_contexts = [for (event in deleted) {event:event,
        entries:placements([for (prior in events where seen(event.context,prior.id)^) prior])^.entries}]
    var i = 0
    while (i < len(rows)) {
        let row = rows[i]
        let competing = alternatives(events,row.id)^
        if (len(competing) > 1) { conflicts = [*conflicts,{kind:'placement',entry:row.id,alternatives:competing}] }
        if (len(row.heads)^ > 1) { conflicts = [*conflicts,{kind:'content',entry:row.id,revisions:row.heads}] }
        if (row.visible and len([for (other in rows where other.visible and other.parent == row.parent and other.name == row.name) other]) > 1) {
            conflicts = [*conflicts,{kind:'name',entry:row.id,parent:row.parent,name:row.name}]
        }
        // inspect each deletion's causal tree, not a receiver's arrival order.
        var j = 0
        while (j < len(deleted_contexts)) {
            let cut = deleted_contexts[j].event
            let before = deleted_contexts[j].entries
            if (not row.visible and ancestor(entries,row.id,cut.entry)^ and not seen(cut.context,row.id)^) {
                conflicts = [*conflicts,{kind:'delete-create',entry:row.id,deletion:cut.id}]
            }
            if (ancestor(before,row.id,cut.entry)^) {
                if (row.visible) {
                    conflicts = [*conflicts,{kind:'delete-move',entry:row.id,deletion:cut.id}]
                } else if (any([for (event in events where event.action == 'publish' and event.entry == row.id)
                    not seen(cut.context,event.id)^])) {
                    conflicts = [*conflicts,{kind:'delete-edit',entry:row.id,deletion:cut.id}]
                }
            }
            j = j+1
        }
        i = i+1
    }
    return {entries:rows,conflicts:conflicts}
}

pub fn path(entries, id, materialized=false) any^ {
    var current = id
    var parts = []
    var remaining = len(entries)+1
    while (current != v.root and remaining > 0) {
        let entry = v.lookup(entries,current)^
        if (entry == null) raise error("sync workspace: missing path entry")
        parts = [if (materialized) entry.materialized_name else entry.name,*parts]
        current = entry.parent
        remaining = remaining-1
    }
    if (remaining == 0) raise error("sync workspace: invalid path cycle")
    return join(parts,"/")^
}
