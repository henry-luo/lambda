// immutable ordered pages keep control records bounded; cursors bind the exact root.
import codec: .codec
import schema: .cloud_schema
import v: .schema

pub let leaf_size = 64
pub let fanout = 32
fn ordered(entries) any^ => sort(entries,(entry)=>entry.id)^
fn reference(id, node) any^ => {id:id,count:node.count,first:node.first,last:node.last}
pub fn describe(node) any^ => reference(schema.digest(node)^,node)^
pub fn leaf_value(entries) any^ {
    let values = ordered(entries)^
    return {version:1,kind:'leaf',count:len(values)^,
        first:values[0].id,last:values[len(values)^-1].id,entries:values}
}
pub fn branch_value(children) any^ => {version:1,kind:'branch',count:sum([for (child in children) child.count]),
    first:children[0].first,last:children[len(children)^-1].last,children:children}
pub fn combined(roots) any^ {
    if (len(roots)^ == 0 or len(roots)^ > fanout) raise error("sync pages: invalid combine fanout")
    let sorted = sort(roots,(root)=>root.first)^
    return if (len(sorted)^ == 1) sorted[0] else describe(branch_value(sorted)^)^
}
pub fn valid_ref(root) any^ {
    v.fields(root,["id","count","first","last"])^
    if (not schema.id(root.id) or not v.natural(root.count)^ or root.count < 1 or
        not (root.first is string) or not (root.last is string) or root.first > root.last)
        raise error("sync pages: invalid reference")
    return true
}
pub pn read(store, prefix, root) any^ {
    valid_ref(root)^
    let found = store.get(prefix ++ "pages/" ++ root.id)^
    if (found == null) raise error("sync pages: missing page")
    let node = codec.decode(found.body,262144)^
    v.record(node,["version","kind","count","first","last"],["entries","children"])^
    if (schema.digest(node)^ != root.id or node.version != 1 or
        root != reference(root.id,node)^) raise error("sync pages: reference binding mismatch")
    if (node.kind == 'leaf') {
        if (not (node.entries is array) or len(node.entries)^ < 1 or len(node.entries)^ > leaf_size or
            node.entries != ordered(node.entries)^ or
            len(unique([for (entry in node.entries) entry.id]))^ != len(node.entries)^ or
            node.count != len(node.entries)^ or node.first != node.entries[0].id or
            node.last != node.entries[len(node.entries)^-1].id)
            raise error("sync pages: invalid leaf")
        for (entry in node.entries) {
            v.fields(entry,["id","value"])^
            if (not (entry.id is string) or len(entry.id)^ == 0) raise error("sync pages: invalid key")
        }
    } else if (node.kind == 'branch') {
        if (not (node.children is array) or len(node.children)^ < 1 or len(node.children)^ > fanout or
            node.count != sum([for (child in node.children) child.count]) or
            node.first != node.children[0].first or node.last != node.children[len(node.children)^-1].last)
            raise error("sync pages: invalid branch")
        for (index in 0 to (len(node.children)^-1)) {
            valid_ref(node.children[index])^
            if (index > 0 and node.children[index-1].last >= node.children[index].first)
                raise error("sync pages: overlapping children")
        }
    } else { raise error("sync pages: unknown node kind") }
    return node
}
pn persist(store, prefix, node) any^ {
    let id = schema.digest(node)^
    let encoded = codec.encode(node)^
    if (len(encoded)^ > 262144) raise error("sync pages: page byte limit")
    let result = store.write(prefix ++ "pages/" ++ id,encoded,null)^
    if (not result.written) {
        let old = store.get(prefix ++ "pages/" ++ id)^
        if (old == null or codec.decode(old.body,262144)^ != node) raise error("sync pages: immutable collision")
    }
    return reference(id,node)^
}
pn leaf(store, prefix, entries) any^ {
    return persist(store,prefix,leaf_value(entries)^)^
}
pub pn concat(store, prefix, roots) any^ {
    if (len(roots)^ == 0) { return null }
    for (index in 0 to (len(roots)^-1)) {
        valid_ref(roots[index])^
        if (index > 0 and roots[index-1].last >= roots[index].first)
            raise error("sync pages: concat requires disjoint sorted ranges")
    }
    var level = roots
    while (len(level)^ > 1) {
        var next_level = []
        for (offset in range(0,len(level)^,fanout)) {
            let children = slice(level,offset,offset+fanout)^
            let root = if (len(children)^ == 1) children[0] else persist(store,prefix,branch_value(children)^)^
            next_level = [*next_level,root]
        }
        level = next_level
    }
    return level[0]
}
pub pn build(store, prefix, entries) any^ {
    let values = ordered(entries)^
    if (len(unique([for (entry in values) entry.id]))^ != len(values)^)
        raise error("sync pages: duplicate key")
    var roots = []
    for (offset in range(0,len(values)^,leaf_size)) {
        roots = [*roots,leaf(store,prefix,slice(values,offset,offset+leaf_size)^)^]
    }
    return concat(store,prefix,roots)^
}
pub pn lookup(store, prefix, root, id, depth=0) any^ {
    if (root == null or id < root.first or id > root.last) { return null }
    if (depth > 64) raise error("sync pages: depth limit")
    let node = read(store,prefix,root)^
    if (node.kind == 'leaf') { return v.lookup(node.entries,id)^ }
    let children = [for (child in node.children where child.first <= id and child.last >= id) child]
    return if (len(children)^ == 0) null else lookup(store,prefix,children[0],id,depth+1)^
}
pub pn upsert(store, prefix, root, id, value, depth=0) any^ {
    if (not (id is string) or len(id)^ == 0 or depth > 64) raise error("sync pages: invalid update")
    if (root == null) { return leaf(store,prefix,[{id:id,value:value}])^ }
    let node = read(store,prefix,root)^
    if (node.kind == 'leaf') {
        let old = v.lookup(node.entries,id)^
        if (old != null and old.value == value) { return root }
        return build(store,prefix,[*[for (entry in node.entries where entry.id != id) entry],{id:id,value:value}])^
    }
    var selected = len(node.children)^-1
    var index = 0
    while (index < len(node.children)^) {
        if (id <= node.children[index].last) { selected = index; break }
        index = index+1
    }
    let child = upsert(store,prefix,node.children[selected],id,value,depth+1)^
    if (child == node.children[selected]) { return root }
    // flatten a split child once, so monotonic insertions do not add a level per key.
    let replacement = read(store,prefix,child)^
    let children = [*slice(node.children,0,selected)^,
        *if (replacement.kind == 'branch') replacement.children else [child],
        *slice(node.children,selected+1)^]
    return concat(store,prefix,children)^
}
pub pn upsert_many(store, prefix, root, entries) any^ {
    var result = root
    for (entry in ordered(entries)^) { result = upsert(store,prefix,result,entry.id,entry.value)^ }
    return result
}
pub pn page(store, prefix, root, cursor=null, limit=64) any^ {
    if (not v.natural(limit)^ or limit < 1 or limit > 128) raise error("sync pages: invalid limit")
    if (root == null) {
        if (cursor != null) raise error("sync pages: cursor for empty root")
        return {entries:[],next:null}
    }
    valid_ref(root)^
    if (cursor != null and cursor.root != root.id) raise error("sync pages: stale cursor")
    var pending = if (cursor == null) [{root:root,offset:0}] else cursor.pending
    if (not (pending is array) or len(pending)^ > 2048) raise error("sync pages: invalid cursor")
    var entries = []
    var visited = 0
    while (len(pending)^ > 0 and len(entries)^ < limit) {
        if (visited >= 256) { break }
        let current = pending[0]
        pending = slice(pending,1)^
        let node = read(store,prefix,current.root)^
        if (node.kind == 'branch') {
            pending = [*[for (child in node.children) {root:child,offset:0}],*pending]
        } else {
            if (not v.natural(current.offset)^ or current.offset > len(node.entries)^)
                raise error("sync pages: invalid leaf cursor")
            let end = min(len(node.entries)^,current.offset+limit-len(entries)^)
            entries = [*entries,*slice(node.entries,current.offset,end)^]
            if (end < len(node.entries)^) { pending = [{root:current.root,offset:end},*pending] }
        }
        visited = visited+1
    }
    return {entries:entries,next:if (len(pending)^ == 0) null else {root:root.id,pending:pending}}
}
pub pn all(store, prefix, root, limit=100000) any^ {
    var cursor = null
    var entries = []
    var more = true
    while (more) {
        let part = page(store,prefix,root,cursor,128)^
        entries = [*entries,*part.entries]
        if (len(entries)^ > limit) raise error("sync pages: collection limit")
        cursor = part.next
        more = cursor != null
    }
    if (root != null and len(entries)^ != root.count) raise error("sync pages: incomplete traversal")
    return entries
}
pub pn contains_root(store, prefix, root, expected, depth=0) any^ {
    if (root == expected) { return true }
    if (root == null or expected == null or depth > 64 or root.count < expected.count or
        root.first > expected.first or root.last < expected.last) { return false }
    let node = read(store,prefix,root)^
    if (node.kind == 'leaf') { return false }
    let containing = [for (child in node.children where
        child.first <= expected.first and child.last >= expected.last) child]
    return len(containing)^ == 1 and contains_root(store,prefix,containing[0],expected,depth+1)^
}
