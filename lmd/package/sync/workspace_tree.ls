// ordered move replay and namespace projection; retained events are the authority.
import v: .schema
import codec: .codec

pub fn seen(context, id) any^ => id == v.root or v.clock_get(context,id[0]) >= id[1]

pub fn events(replica) any^ => sort([for (atom in replica.atoms)
    {*:atom.value.event,id:atom.id,time:atom.time,context:v.lookup(replica.receipts,atom.id)^.context}],
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

pub fn deletions(events, entry) any^ => [for (event in events where event.action == 'delete' and event.entry == entry and
    not any([for (other in events where other.action == 'restore' and other.entry == entry)
        contains(other.deletions,event.id)^])) event]

pub fn revisions(events, entry) any^ => sort(unique([for (event in events where
    (event.action == 'create' and event.id == entry and event.type == 'file') or
    (event.action == 'publish' and event.entry == entry)) event.revision]),(revision)=>revision.id)^

pub fn heads(events, entry) any^ {
    let versions = revisions(events,entry)^
    return [for (revision in versions where not any([for (other in versions) contains(other.parents,revision.id)^])) revision]
}

pub fn hidden(entries, events, id) any^ => any([for (entry in entries where ancestor(entries,id,entry.id)^)
    len(deletions(events,entry.id)^) > 0])

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
    let visible_names = names([for (entry in entries where entry.id != v.root and not hidden(entries,events,entry.id)^) entry])^
    let rows = [for (entry in entries where entry.id != v.root)
        {*:entry,visible:not hidden(entries,events,entry.id)^,
         materialized_name:v.lookup(visible_names,entry.id)^.name or entry.name,
         deletions:[for (event in deletions(events,entry.id)^) event.id],
         heads:if (entry.type == 'file') heads(events,entry.id)^ else []}]
    var conflicts = [for (move in replay.suppressed) {kind:'cycle',*:move}]
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
        let deletes = [for (event in events where event.action == 'delete' and
            contains([for (active in deletions(events,event.entry)^) active.id],event.id)^) event]
        var j = 0
        while (j < len(deletes)) {
            let deleted = deletes[j]
            let before = placements([for (event in events where seen(deleted.context,event.id)^) event])^.entries
            if (not row.visible and ancestor(entries,row.id,deleted.entry)^ and not seen(deleted.context,row.id)^) {
                conflicts = [*conflicts,{kind:'delete-create',entry:row.id,deletion:deleted.id}]
            }
            if (ancestor(before,row.id,deleted.entry)^) {
                if (row.visible) {
                    conflicts = [*conflicts,{kind:'delete-move',entry:row.id,deletion:deleted.id}]
                } else if (any([for (event in events where event.action == 'publish' and event.entry == row.id)
                    not seen(deleted.context,event.id)^])) {
                    conflicts = [*conflicts,{kind:'delete-edit',entry:row.id,deletion:deleted.id}]
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
