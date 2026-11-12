// compositional Mark-oriented tree; IDs are metadata, never user map fields.
import c: .core
import seq: .sequence
import text: .text
import codec: .codec

fn key_text(key) any^ {
    if (not (key is string or key is symbol) or (key is symbol and symbol(string(key)) != key))
        raise error("sync: map key requires a lossless name adapter")
    return string(key)
}

// the document root is an MV register, so replacing a root type is an ordinary edit.
pub fn content_node(replica, owner=c.root) any^ {
    if (c.node(replica,owner)^.kind != 'document') { return owner }
    let entries = c.field(replica,owner,"value")
    if (len(entries) != 1 or entries[0].value.kind != 'node') raise error("sync: root is not one editable container")
    return entries[0].value.id
}

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
    if (kind == 'element') {
        codec.encode(name(value))^
        return {kind:'element',tag:name(value),attributes:normalize(map(value))^,
            children:[for (child in content(value)^) normalize(child)^]}
    }
    if (kind == 'array') { return [for (child in value) normalize(child)^] }
    if (kind == 'map') {
        var result = {}
        let keys = [for (key at value) key]
        var i = 0
        while (i < len(keys)) {
            let key = keys[i]
            result = {*:result,[key_text(key)^]:normalize(value[key])^}
            i = i+1
        }
        return result
    }
    codec.encode(value)^
    return value
}

fn known_keys(replica, owner) any^ => unique([for (item in c.sequence(replica,owner,'keys')^) item.value.value])

pub fn keys(replica, requested_owner=c.root) any^ {
    let owner = content_node(replica,requested_owner)^
    let original = known_keys(replica,owner)^
    let explicit = c.metadata(replica,owner,"keys")
    if (len(explicit) > 1) raise error("sync: unresolved map order conflict")
    return if (len(explicit) == 0) original else unique([*explicit[0].value,*original])
}

pub fn reorder(replica, actor, names, requested_owner=c.root) any^ {
    let owner = content_node(replica,requested_owner)^
    let known = known_keys(replica,owner)^
    if (not (names is array) or len(unique(names)) != len(names) or sort(names) != sort(known))
        raise error("sync: map order must be a permutation of known keys")
    return c.set_meta(replica,actor,owner,"keys",names)^
}

pub fn tag(replica, actor, name, owner=c.root) any^ => c.set_meta(replica,actor,content_node(replica,owner)^,"tag",name)^

pub fn tag_name(replica, requested_owner=c.root) any^ {
    let owner = content_node(replica,requested_owner)^
    let entries = c.metadata(replica,owner,"tag")
    if (len(entries) > 1) raise error("sync: unresolved element tag conflict")
    return if (len(entries) == 0) c.node(replica,owner)^.tag else entries[0].value
}

pub fn resolve_field(replica, actor, key, alternative, requested_owner=c.root) any^ {
    let owner = content_node(replica,requested_owner)^
    let chosen = c.lookup(c.field(replica,owner,string(key)),alternative)^
    if (chosen == null) raise error("sync: conflict alternative is absent")
    return c.set_field(replica,actor,owner,string(key),chosen.value)^
}

pub fn replace_scalar(replica, actor, value, owner=c.root) any^ => c.set_field(replica,actor,owner,"value",c.scalar(value))^

fn ensure_key(replica, actor, owner, key) any^ {
    let existing = known_keys(replica,owner)^
    if (contains(existing,key)^) { return replica }
    return seq.insert(replica,actor,owner,'keys',len(c.sequence(replica,owner,'keys')^),[c.scalar(key)])^
}

fn build_value(replica, actor, owner, value) any^ {
    let kind = kind_of(value)^
    if (kind == 'scalar') { return {replica:replica,value:c.scalar(value)} }
    let created = c.make_node(replica,actor,owner,kind,if (kind == 'element') name(value) else null)^
    return {replica:fill_node(created.replica,actor,created.id,value)^,value:c.reference(created.id)}
}

pub fn set(replica, actor, requested_key, value, requested_owner=c.root) any^ {
    let owner = content_node(replica,requested_owner)^
    let key = key_text(requested_key)^
    let existing = c.node(replica,owner)^
    if (existing == null or not contains(['map','element'],existing.kind)^) raise error("sync: expected map or element")
    let indexed = ensure_key(replica,actor,owner,string(key))^
    let built = build_value(indexed,actor,owner,value)^
    return c.set_field(built.replica,actor,owner,string(key),built.value)^
}

pub fn remove_field(replica, actor, key, owner=c.root) any^ =>
    c.set_field(replica,actor,content_node(replica,owner)^,key_text(key)^,c.absent())^

pub fn set_blob(replica, actor, requested_key, descriptor, requested_owner=c.root) any^ {
    let owner = content_node(replica,requested_owner)^
    let key = key_text(requested_key)^
    if (descriptor.kind != 'blob') raise error("sync: expected blob descriptor")
    return c.set_field(ensure_key(replica,actor,owner,string(key))^,actor,owner,string(key),descriptor)^
}

pub fn splice(replica, actor, start, count, values, requested_owner=c.root) any^ {
    let owner = content_node(replica,requested_owner)^
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
    return replace_root(c.empty(document,'mark','document',null),actor,value)^
}

pub fn replace_root(replica, actor, value) any^ {
    if (c.node(replica,c.root)^.kind != 'document') raise error("sync: root replacement requires a document root")
    let built = build_value(replica,actor,c.root,value)^
    return c.set_field(built.replica,actor,c.root,"value",built.value)^
}

pub fn resolve_root(replica, actor, alternative) any^ {
    let chosen = c.lookup(c.field(replica,c.root,"value"),alternative)^
    if (chosen == null) raise error("sync: root alternative is absent")
    return c.set_field(replica,actor,c.root,"value",chosen.value)^
}

pub fn child(replica, key, requested_owner=c.root) any^ {
    let owner = content_node(replica,requested_owner)^
    let cells = c.field(replica,owner,string(key))
    if (len(cells) != 1 or cells[0].value.kind != 'node') raise error("sync: expected one child node")
    return cells[0].value.id
}

fn project_value(replica, descriptor, native=false) any^ =>
    if (descriptor.kind == 'node') value_at(replica,descriptor.id,native)^
    else if (descriptor.kind == 'scalar') descriptor.value
    else if (descriptor.kind == 'blob') descriptor
    else raise error("sync: absent value")

fn project_fields(replica, owner, native=false) any^ {
    let names = keys(replica,owner)^
    var result = {}
    var i = 0
    while (i < len(names)) {
        let key = names[i]
        let cells = c.field(replica,owner,key)^
        if (len(cells) > 1) raise error("sync: unresolved field conflict")
        if (len(cells) == 1 and cells[0].value.kind != 'absent') {
            result = {*:result,[key]:project_value(replica,cells[0].value,native)^}
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

pub fn value_at(replica, owner, native=false) any^ {
    let item = c.node(replica,owner)^
    if (item == null) raise error("sync: unknown Mark node")
    if (item.kind == 'text') { return text.read(replica,owner)^ }
    if (item.kind == 'map') { return project_fields(replica,owner,native)^ }
    if (item.kind == 'scalar' or item.kind == 'document') {
        let entries = c.field(replica,owner,"value")
        if (len(entries) != 1) raise error("sync: unresolved field conflict")
        return project_value(replica,entries[0].value,native)^
    }
    let children = [for (atom in c.sequence(replica,owner,'items')^) project_value(replica,atom.value,native)^]
    if (item.kind == 'array') { return children }
    let attrs = project_fields(replica,owner,native)^
    let tag = tag_name(replica,owner)^
    if (native) { return <[tag] *:attrs, *children> }
    return {kind:'element',tag:tag,attributes:attrs,children:normalized_content(children)^}
}

pub fn value(replica) any^ => value_at(replica,c.root)^
// construct elements from node kinds, never by interpreting user map field names.
pub fn export_value(replica) any^ => value_at(replica,c.root,true)^

fn descendants(replica, owner) any^ {
    let refs = [*[for (cell in replica.cells where cell.node == owner and cell.value.kind == 'node') cell.value.id],
        *[for (atom in c.sequence(replica,owner,'items')^ where atom.value.kind == 'node') atom.value.id]]
    return [owner,*[for (id in unique(refs)) for (child_id in descendants(replica,id)^) child_id]]
}

pub fn reachable(replica) any^ => unique(descendants(replica,c.root)^)

pub fn conflicts(replica) any^ {
    let fields = unique([for (cell in replica.cells) [cell.node,cell.key]])
    let meta_fields = unique([for (cell in replica.meta) [cell.node,cell.key]])
    return [*[for (field in fields, let entries=c.field(replica,field[0],field[1])^ where len(entries)>1)
        {node:field[0],key:field[1],alternatives:entries}],
        *[for (field in meta_fields, let entries=c.metadata(replica,field[0],field[1])^ where len(entries)>1)
        {node:field[0],key:field[1],metadata:true,alternatives:entries}]]
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
    let desired = [*[for (key at external) key_text(key)^],*[for (key in keys(result,owner)^ where not contains(external,key)^) key]]
    return if (desired == keys(result,owner)^) result else reorder(result,actor,desired,owner)^
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
    if (item.kind == 'document') {
        let entries = c.field(replica,owner,"value")
        if (len(entries) != 1) raise error("sync: ambiguous structured base")
        let prior = entries[0].value
        if (prior.kind == 'node' and c.node(replica,prior.id)^.kind == kind) {
            return reconcile_at(replica,actor,prior.id,external)^
        }
        return if (prior.kind == 'scalar' and prior.value == external) replica else replace_root(replica,actor,external)^
    }
    if (item == null or item.kind != kind or (kind == 'element' and tag_name(replica,owner)^ != name(external)))
        raise error("sync: incompatible structured replacement")
    if (codec.digest(value_at(replica,owner)^)^ == codec.digest(normalize(external)^)^) { return replica }
    if (kind == 'text') { return text.reconcile(replica,actor,external,owner)^ }
    if (kind == 'scalar') { return c.set_field(replica,actor,owner,"value",c.scalar(external))^ }
    if (kind == 'map') { return reconcile_fields(replica,actor,owner,external)^ }
    if (kind == 'array') { return reconcile_items(replica,actor,owner,external)^ }
    return reconcile_items(reconcile_fields(replica,actor,owner,map(external))^,
        actor,owner,[for (child in content(external)^) child])^
}

pub fn reconcile(replica, actor, external) any^ => reconcile_at(replica,actor,c.root,external)^
