// compositional Mark-oriented tree; IDs are metadata, never user map fields.
import c: .core
import seq: .sequence
import text: .text

fn kind_of(value) any^ {
    if (value is map) { return 'map' }
    if (value is array) { return 'array' }
    if (value is element) { return 'element' }
    if (value is string) { return 'text' }
    if (value is null or value is bool or value is int or value is float or
        value is integer or value is decimal or value is symbol or value is binary or value is datetime) { return 'scalar' }
    raise error("sync: unsupported Mark prototype value")
}

pub fn normalize(value) any^ {
    let kind = kind_of(value)^
    if (kind == 'element') { return {kind:'element',tag:name(value),
        attributes:normalize(map(value))^, children:[for (child in content(value)^) normalize(child)^]} }
    if (kind == 'array') { return [for (child in value) normalize(child)^] }
    if (kind == 'map') {
        var result = {}
        let keys = [for (key at value) key]
        var i = 0
        while (i < len(keys)) {
            let key = keys[i]
            result = {*:result,[string(key)]:normalize(value[key])^}
            i = i+1
        }
        return result
    }
    return value
}

pub fn keys(replica, owner=c.root) any^ =>
    unique([for (item in c.sequence(replica,owner,'keys')^) item.value.value])

fn ensure_key(replica, actor, owner, key) any^ {
    let existing = keys(replica,owner)^
    if (contains(existing,key)^) { return replica }
    return seq.insert(replica,actor,owner,'keys',len(c.sequence(replica,owner,'keys')^),[c.scalar(key)])^
}

fn build_value(replica, actor, owner, value) any^ {
    let kind = kind_of(value)^
    if (kind == 'scalar') { return {replica:replica,value:c.scalar(value)} }
    let created = c.make_node(replica,actor,owner,kind,if (kind == 'element') name(value) else null)^
    return {replica:fill_node(created.replica,actor,created.id,value)^,value:c.reference(created.id)}
}

pub fn set(replica, actor, key, value, owner=c.root) any^ {
    let existing = c.node(replica,owner)^
    if (existing == null or not contains(['map','element'],existing.kind)^) raise error("sync: expected map or element")
    let indexed = ensure_key(replica,actor,owner,string(key))^
    let built = build_value(indexed,actor,owner,value)^
    return c.set_field(built.replica,actor,owner,string(key),built.value)^
}

pub fn remove_field(replica, actor, key, owner=c.root) any^ =>
    c.set_field(replica,actor,owner,string(key),c.absent())^

pub fn splice(replica, actor, start, count, values, owner=c.root) any^ {
    let existing = c.node(replica,owner)^
    if (existing == null or not contains(['array','element'],existing.kind)^) raise error("sync: expected array or element")
    var result = seq.remove(replica,actor,owner,'items',start,count)^
    var index = start
    var i = 0
    while (i < len(values)) {
        let built = build_value(result,actor,owner,values[i])^
        result = seq.insert(built.replica,actor,owner,'items',index,[built.value])^
        index = index+1
        i = i+1
    }
    return result
}

fn fill_node(replica, actor, owner, value) any^ {
    let kind = kind_of(value)^
    if (kind == 'text') { return text.insert(replica,actor,0,value,owner)^ }
    if (kind == 'scalar') { return c.set_field(replica,actor,owner,"value",c.scalar(value))^ }
    var result = replica
    if (kind == 'map' or kind == 'element') {
        let attrs = if (kind == 'element') map(value) else value
        let names = [for (key at attrs) key]
        var i = 0
        while (i < len(names)) {
            result = set(result,actor,names[i],attrs[names[i]],owner)^
            i = i+1
        }
    }
    if (kind == 'array' or kind == 'element') {
        result = splice(result,actor,0,0,if (kind == 'element') [for (x in content(value)^) x] else value,owner)^
    }
    return result
}

pub fn from_value(document, actor, value) any^ {
    let kind = kind_of(value)^
    let initial = c.empty(document,'mark',kind,if (kind == 'element') name(value) else null)
    return fill_node(initial,actor,c.root,value)^
}

pub fn child(replica, key, owner=c.root) any^ {
    let cells = c.field(replica,owner,string(key))
    if (len(cells) != 1 or cells[0].value.kind != 'node') raise error("sync: expected one child node")
    return cells[0].value.id
}

fn project_value(replica, descriptor) any^ =>
    if (descriptor.kind == 'node') value_at(replica,descriptor.id)^
    else if (descriptor.kind == 'scalar') descriptor.value
    else raise error("sync: absent value")

fn project_fields(replica, owner) any^ {
    let names = keys(replica,owner)^
    var result = {}
    var i = 0
    while (i < len(names)) {
        let key = names[i]
        let cells = c.field(replica,owner,key)^
        if (len(cells) > 1) raise error("sync: unresolved field conflict")
        if (len(cells) == 1 and cells[0].value.kind != 'absent') {
            result = {*:result,[key]:project_value(replica,cells[0].value)^}
        }
        i = i+1
    }
    return result
}

// normalize the inspection projection, retaining all editing anchors in the core.
fn normalized_content(children) any^ {
    var result = []
    var i = 0
    while (i < len(children)) {
        let child = children[i]
        if (child != null and child != "") {
            let previous = if (len(result) == 0) null else result[len(result)-1]
            if ((child is string and previous is string) or (child is binary and previous is binary)) {
                result = [*take(result,len(result)-1),previous ++ child]
            } else { result = [*result,child] }
        }
        i = i+1
    }
    return result
}

pub fn value_at(replica, owner) any^ {
    let item = c.node(replica,owner)^
    if (item == null) raise error("sync: unknown Mark node")
    if (item.kind == 'text') { return text.read(replica,owner)^ }
    if (item.kind == 'map') { return project_fields(replica,owner)^ }
    if (item.kind == 'scalar') {
        let entries = c.field(replica,owner,"value")
        if (len(entries) != 1) raise error("sync: unresolved field conflict")
        return project_value(replica,entries[0].value)^
    }
    let children = [for (atom in c.sequence(replica,owner,'items')^) project_value(replica,atom.value)^]
    if (item.kind == 'array') { return children }
    // an inspection model, not a native-element export/format adapter.
    return {kind:'element',tag:item.tag,attributes:project_fields(replica,owner)^,children:normalized_content(children)^}
}

pub fn value(replica) any^ => value_at(replica,c.root)^

fn descendants(replica, owner) any^ {
    let refs = [*[for (cell in replica.cells where cell.node == owner and cell.value.kind == 'node') cell.value.id],
        *[for (atom in c.sequence(replica,owner,'items')^ where atom.value.kind == 'node') atom.value.id]]
    return [owner,*[for (id in unique(refs)) for (child_id in descendants(replica,id)^) child_id]]
}

pub fn reachable(replica) any^ => unique(descendants(replica,c.root)^)

pub fn conflicts(replica) any^ {
    let fields = unique([for (cell in replica.cells) [cell.node,cell.key]])
    return [for (field in fields, let entries=c.field(replica,field[0],field[1])^ where len(entries)>1)
        {node:field[0],key:field[1],alternatives:entries}]
}

fn reconcile_fields(replica, actor, owner, external) any^ {
    let original = keys(replica,owner)^
    let names = unique([*original,*[for (key at external) string(key)]])
    var result = replica
    var i = 0
    while (i < len(names)) {
        let key = names[i]
        let cells = c.field(replica,owner,key)^
        if (len(cells) > 1) raise error("sync: ambiguous structured base")
        if (not contains(external,key)^) {
            if (len(cells) == 1 and cells[0].value.kind != 'absent') {
                result = remove_field(result,actor,key,owner)^
            }
        } else {
            let prior = (if (len(cells) == 1) cells[0].value else c.absent())^
            let fresh = external[key]^
            if (prior.kind == 'node' and c.node(replica,prior.id)^.kind == kind_of(fresh)^) {
                result = reconcile_at(result,actor,prior.id,fresh)^
            } else if (prior.kind != 'scalar' or prior.value != fresh) {
                result = set(result,actor,key,fresh,owner)^
            }
        }
        i = i+1
    }
    return result
}

fn reconcile_items(replica, actor, owner, external) any^ {
    let items = c.sequence(replica,owner,'items')^
    if (len(items) == 1 and len(external) == 1 and items[0].value.kind == 'node' and
        c.node(replica,items[0].value.id)^.kind == 'text' and external[0] is string) {
        return text.reconcile(replica,actor,external[0],items[0].value.id)^
    }
    let before = [for (item in items) project_value(replica,item.value)^]
    let after = [for (item in external) normalize(item)^]
    let alignment = seq.correspondence(before,after)^
    if (alignment.ambiguous) raise error("sync: ambiguous structured sequence correspondence")
    let edits = alignment.changes^
    let removed = [for (change in edits where change.kind == 'delete') items[change.before]]
    let added = [for (change in edits where change.kind == 'insert') external[change.after]]
    // changed container correspondences need schema/IDs; matching by position is unsafe.
    if (any([for (item in removed) item.value.kind == 'node' and c.node(replica,item.value.id)^.kind != 'text']) and
        any([for (item in added) contains(['map','array','element'],kind_of(item)^)]))
        raise error("sync: ambiguous structured sequence correspondence")
    var result = c.delete_ids(replica,actor,owner,'items',[for (item in removed) item.id])^
    var left = null
    var i = 0
    while (i < len(edits)) {
        let change = edits[i]
        if (change.kind == 'keep') { left = items[change.before].id }
        if (change.kind == 'insert') {
            let built = build_value(result,actor,owner,external[change.after])^
            let id = c.next_id(built.replica,actor)
            result = c.insert_after(built.replica,actor,owner,'items',left,built.value)^
            left = id
        }
        i = i+1
    }
    return result
}

pub fn reconcile_at(replica, actor, owner, external) any^ {
    let item = c.node(replica,owner)^
    let kind = kind_of(external)^
    if (item == null or item.kind != kind or (kind == 'element' and item.tag != name(external)))
        raise error("sync: incompatible structured replacement")
    if (value_at(replica,owner)^ == normalize(external)^) { return replica }
    if (kind == 'text') { return text.reconcile(replica,actor,external,owner)^ }
    if (kind == 'scalar') { return c.set_field(replica,actor,owner,"value",c.scalar(external))^ }
    if (kind == 'map') { return reconcile_fields(replica,actor,owner,external)^ }
    if (kind == 'array') { return reconcile_items(replica,actor,owner,external)^ }
    return reconcile_items(reconcile_fields(replica,actor,owner,map(external))^,
        actor,owner,[for (child in content(external)^) child])^
}

pub fn reconcile(replica, actor, external) any^ => reconcile_at(replica,actor,c.root,external)^
