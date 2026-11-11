// experimental state-based RGA + causal multi-value registers; S12.1.1v3.
// references and limits: vibe/impl/Lambda_Impl_Sync_Prototype (done).md.

pub let root = ["", 0]

pub fn clock_get(clock, actor) {
    let found = [for (entry in clock where entry.actor == actor) entry.counter]
    if (len(found) == 0) 0 else found[0]
}

pub fn covered(clock, id) any^ => id != null and clock_get(clock, id[0]) >= id[1]

pub fn dominates(a, b) => all([for (entry in b) clock_get(a, entry.actor) >= entry.counter])

fn clock_join(a, b) => [for (actor in sort(unique([*[for (x in a) x.actor], *[for (x in b) x.actor]])))
    {actor: actor, counter: max(clock_get(a, actor), clock_get(b, actor))}]

fn clock_add(clock, id) => clock_join(clock, [{actor:id[0], counter:id[1]}])

pub fn next_id(replica, actor) => [actor, clock_get(replica.clock, actor) + 1]

pub fn scalar(value) => {kind:'scalar', value:value}
pub fn reference(id) => {kind:'node', id:id}
pub fn absent() => {kind:'absent'}

pub fn empty(document, profile, kind, tag) => {
    protocol:1, document:document, profile:profile, epoch:0, time:0,
    clock:[], floor:[], receipts:[],
    nodes:[{id:root, kind:kind, tag:tag, parent:null}],
    atoms:[], dead:[], cells:[], log:[], pending:[]
}

pub fn lookup(entries, id) {
    let found = [for (entry in entries where entry.id == id) entry]
    if (len(found) == 0) null else found[0]
}

pub fn node(replica, id) any^ => lookup(replica.nodes,id)^

fn by_id(entries) any^ => sort(entries, (entry) => entry.id)^
fn has_id(entries, id) any^ => lookup(entries,id)^ != null
fn receipt(replica, id) any^ => lookup(replica.receipts,id)^

fn compatible(a, b) any^ {
    if (a.protocol != 1 or b.protocol != 1 or a.document != b.document or
        a.profile != b.profile or a.epoch != b.epoch or a.nodes[0] != b.nodes[0])
        raise error("sync: incompatible document, profile, root or epoch")
    return true
}

fn stamp(op) any^ => hash(format([op.protocol, op.document, op.profile, op.epoch, op.id,
    op.time, op.context, op.kind, op.node, op.slot, op.key, op.left, op.value], 'mark')^, 'sha256')^

fn check_receipt(replica, op) any^ {
    let old = receipt(replica, op.id)^
    if (old != null and old.hash != stamp(op)^) raise error("sync: actor operation ID reused")
    return old != null
}

fn validate_envelope(replica, op) any^ {
    if (op.protocol != 1 or op.document != replica.document or op.profile != replica.profile or op.epoch != replica.epoch)
        raise error("sync: incompatible operation")
    if (not (op.id is array) or len(op.id) != 2 or not (op.id[0] is string) or
        op.id[0] == "" or not (op.id[1] is int) or op.id[1] <= 0 or
        not (op.time is int) or op.time <= 0 or not (op.context is array))
        raise error("sync: invalid actor, counter or causal context")
    if (clock_get(op.context, op.id[0]) != op.id[1] - 1)
        raise error("sync: non-contiguous actor context")
    var index = 0
    while (index < len(op.context)^) {
        let entry = op.context[index]
        if (not (entry.actor is string) or entry.actor == "" or not (entry.counter is int) or entry.counter <= 0)
            raise error("sync: invalid causal context entry")
        index = index+1
    }
    if (op.context != clock_join(op.context,[])) raise error("sync: noncanonical causal context")
    return true
}

fn check_value(replica, owner, value) any^ {
    if (value.kind == 'node') {
        let target = node(replica, value.id)^
        if (target == null or target.parent != owner) raise error("sync: invalid child reference")
    } else if (value.kind != 'scalar' and value.kind != 'absent') {
        raise error("sync: invalid value descriptor")
    }
    return true
}

fn apply_ready(replica, op) any^ {
    let owner = node(replica, op.node)^
    var nodes = replica.nodes
    var atoms = replica.atoms
    var dead = replica.dead
    var cells = replica.cells
    if (op.kind == 'node') {
        if (owner == null or not contains(['map','array','element'],owner.kind)^ or
            not contains(['map','array','element','text','scalar'], op.value.kind))
            raise error("sync: invalid new node")
        nodes = by_id([*nodes, {id:op.id, kind:op.value.kind, tag:op.value.tag, parent:op.node}])
    }
    if (op.kind == 'insert') {
        if (owner == null or not contains(['items','keys'], op.slot)) raise error("sync: invalid sequence")
        if ((op.slot == 'keys' and not contains(['map','element'],owner.kind)^) or
            (op.slot == 'items' and not contains(['text','array','element'],owner.kind)^))
            raise error("sync: invalid sequence owner")
        if (op.value.kind == 'absent' or
            (op.slot == 'keys' and (op.value.kind != 'scalar' or not (op.value.value is string))) or
            (owner.kind == 'text' and (op.value.kind != 'scalar' or not (op.value.value is string) or len(op.value.value) != 1)))
            raise error("sync: invalid sequence payload")
        if (op.left != null and not any([for (atom in atoms)
            atom.id == op.left and atom.node == op.node and atom.slot == op.slot]))
            raise error("sync: insertion anchor is absent")
        check_value(replica, op.node, op.value)^
        atoms = by_id([*atoms, {id:op.id, node:op.node, slot:op.slot,
            left:op.left, time:op.time, value:op.value}])
    }
    if (op.kind == 'delete') {
        if (owner == null or not (op.value is array)) raise error("sync: invalid deletion")
        var index = 0
        while (index < len(op.value)^) {
            let id = op.value[index]
            if (not any([for (atom in atoms) atom.id == id and atom.node == op.node and atom.slot == op.slot]))
                raise error("sync: deletion target is absent")
            index = index+1
        }
        dead = sort(unique([*dead, *op.value]))
    }
    if (op.kind == 'set') {
        if (owner == null or not (op.key is string)) raise error("sync: invalid field")
        if (not contains(['map','element','scalar'],owner.kind)^ or (owner.kind == 'scalar' and op.key != "value"))
            raise error("sync: invalid field owner")
        check_value(replica, op.node, op.value)^
        // delayed delivery must keep assignments not observed by this writer.
        cells = by_id([*[for (cell in cells where cell.node != op.node or cell.key != op.key or
            not covered(op.context,cell.id)^) cell],
            {id:op.id, node:op.node, key:op.key, value:op.value}])
    }
    if (not contains(['node','insert','delete','set'],op.kind)) raise error("sync: unknown operation")
    return {*:replica, time:max(replica.time, op.time), clock:clock_add(replica.clock, op.id),
        nodes:nodes, atoms:atoms, dead:dead, cells:cells,
        receipts:by_id([*replica.receipts, {id:op.id, hash:stamp(op)^}]), log:by_id([*replica.log, op])}
}

fn drain(replica, incoming) any^ {
    var current = {*:replica, pending:[]}
    var waiting = []
    var at = 0
    while (at < len(incoming)) {
        let op = incoming[at]
        validate_envelope(current,op)^
        let old = lookup(waiting,op.id)^
        if (old != null and stamp(old)^ != stamp(op)^) raise error("sync: actor operation ID reused")
        if (old == null) { waiting = [*waiting,op] }
        at = at + 1
    }
    waiting = by_id(waiting)^
    var progress = true
    while (progress and len(waiting) > 0) {
        progress = false
        var rest = []
        var index = 0
        while (index < len(waiting)) {
            let op = waiting[index]
            validate_envelope(current, op)^
            if (check_receipt(current, op)^) { progress = true }
            else if (dominates(current.clock, op.context)^) {
                current = apply_ready(current, op)^
                progress = true
            } else { rest = [*rest, op] }
            index = index + 1
        }
        waiting = rest
    }
    return {*:current, pending:waiting}
}

pub fn receive(replica, operations) any^ => drain(replica, [*replica.pending, *operations])^

fn issue(replica, actor, kind, owner, slot, key, left, value) any^ {
    if (not (actor is string) or actor == "") raise error("sync: actor must be nonempty text")
    let op = {protocol:1, document:replica.document, profile:replica.profile, epoch:replica.epoch,
        id:next_id(replica, actor), time:replica.time + 1, context:replica.clock,
        kind:kind, node:owner, slot:slot, key:key, left:left, value:value}
    return receive(replica, [op])^
}

pub fn make_node(replica, actor, owner, kind, tag) any^ {
    let id = next_id(replica, actor)
    return {replica:issue(replica, actor, 'node', owner, null, null, null, {kind:kind, tag:tag})^, id:id}
}

pub fn insert_after(replica, actor, owner, slot, left, value) any^ =>
    issue(replica, actor, 'insert', owner, slot, null, left, value)^

pub fn delete_ids(replica, actor, owner, slot, ids) any^ =>
    if (len(ids) == 0) replica else issue(replica, actor, 'delete', owner, slot, null, null, ids)^

pub fn set_field(replica, actor, owner, key, value) any^ =>
    issue(replica, actor, 'set', owner, null, key, null, value)^

// RGA preorder: newer siblings precede older siblings; removed anchors stay.
fn walk(replica, owner, slot, left) any^ {
    let children = sort([for (atom in replica.atoms where atom.node == owner and
        atom.slot == slot and atom.left == left) atom], {dir:'desc', by:(atom)=>[atom.time, atom.id]});
    return [for (atom in children) for (item in [atom, *walk(replica, owner, slot, atom.id)^]) item]
}

pub fn sequence(replica, owner, slot) any^ =>
    [for (atom in walk(replica, owner, slot, null)^ where not contains(replica.dead, atom.id)^) atom]

pub fn field(replica, owner, key) => [for (cell in replica.cells where cell.node == owner and cell.key == key) cell]

fn check_forks(a, b) any^ {
    var index = 0
    while (index < len(a.receipts)^) {
        let entry = a.receipts[index]
        let other = receipt(b, entry.id)^
        if (other != null and entry.hash != other.hash) raise error("sync: actor operation ID reused")
        index = index+1
    }
    return true
}

fn union_by_id(a, b) any^ => by_id([*a, *[for (entry in b where not has_id(a, entry.id)^) entry]])^

pub fn merge(a, b) any^ {
    compatible(a, b)^
    check_forks(a, b)^
    let dead = sort(unique([*a.dead, *b.dead]))
    let floor = clock_join(a.floor, b.floor)
    // a missing dot that the other context has seen was causally overwritten.
    let cells = union_by_id(
        [for (cell in a.cells where has_id(b.cells, cell.id)^ or not covered(b.clock, cell.id)^) cell],
        [for (cell in b.cells where has_id(a.cells, cell.id)^ or not covered(a.clock, cell.id)^) cell])^
    let atoms = [for (atom in union_by_id(a.atoms, b.atoms)^)
        if (contains(dead, atom.id) and (atom.value == null or
            any([for (other in [*a.atoms, *b.atoms]) other.id == atom.id and other.value == null])))
            {*:atom, value:null} else atom]
    let merged = {*:a, time:max(a.time,b.time), clock:clock_join(a.clock,b.clock), floor:floor,
        receipts:union_by_id(a.receipts,b.receipts), nodes:union_by_id(a.nodes,b.nodes),
        atoms:atoms, dead:dead, cells:cells,
        log:[for (op in union_by_id(a.log,b.log)^ where not covered(floor,op.id)^) op], pending:[]}
    return drain(merged, [*a.pending, *b.pending])^
}

pub fn compact(replica) => {*:replica, floor:replica.clock, log:[],
    atoms:[for (atom in replica.atoms) if (contains(replica.dead,atom.id)) {*:atom,value:null} else atom]}

// a compacted sender supplies state when the requested packet prefix is gone.
pub fn packet(replica, peer) any^ {
    compatible(replica, peer)^
    return if (not dominates(peer.clock, replica.floor)) {kind:'checkpoint', replica:compact(replica)}
    else {kind:'updates', operations:[for (op in replica.log where not covered(peer.clock,op.id)^) op]}
}

pub fn receive_packet(replica, message) any^ =>
    if (message.kind == 'checkpoint') merge(replica,message.replica)^
    else if (message.kind == 'updates') receive(replica,message.operations)^
    else raise error("sync: unknown packet kind")

pub fn snapshot(replica) any^ {
    if (len(replica.pending) != 0) raise error("sync: cannot save with unresolved dependencies")
    let id = hash(format([replica.document,replica.profile,replica.epoch,replica.nodes[0],replica.receipts], 'mark')^,'sha256')^
    return {protocol:1, id:id, document:replica.document, epoch:replica.epoch, frontier:replica.clock, replica:compact(replica)}
}

pub fn restore(saved) => saved.replica

