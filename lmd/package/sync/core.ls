// experimental state-based RGA + causal multi-value registers; S12.1.1v3.
// references and limits: vibe/impl/Lambda_Impl_Sync_Prototype (done).md.
import v: .schema
import codec: .codec
import workspace: .workspace_rules

pub let root = v.root

pub fn clock_get(clock, actor) => v.clock_get(clock,actor)

pub fn covered(clock, id) any^ => id != null and clock_get(clock, id[0]) >= id[1]

pub fn dominates(a, b) => all([for (entry in b) clock_get(a, entry.actor) >= entry.counter])

pub fn clock_join(a, b) => [for (actor in sort(unique([*[for (x in a) x.actor], *[for (x in b) x.actor]])))
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
    atoms:[], dead:[], cells:[], meta:[], log:[], pending:[]
}

pub let lookup = v.lookup

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

pub fn stamp(op) any^ => codec.digest([op.protocol, op.document, op.profile, op.epoch, op.id,
    op.time, op.context, op.kind, op.node, op.slot, op.key, op.left, op.value])^

fn check_receipt(replica, op) any^ {
    let old = receipt(replica, op.id)^
    if (old != null and old.hash != stamp(op)^) raise error("sync: actor operation ID reused")
    return old != null
}

fn validate_envelope(replica, op) any^ {
    v.fields(op,["protocol","document","profile","epoch","id","time","context","kind","node","slot","key","left","value"])^
    if (op.protocol != 1 or op.document != replica.document or op.profile != replica.profile or op.epoch != replica.epoch)
        raise error("sync: incompatible operation")
    if (not v.id(op.id)^ or not v.natural(op.time)^ or op.time <= 0 or not (op.context is array))
        raise error("sync: invalid actor, counter or causal context")
    if (clock_get(op.context, op.id[0]) != op.id[1] - 1)
        raise error("sync: non-contiguous actor context")
    v.clock(op.context)^
    if (not v.id(op.node,true)^ or (op.node != root and not covered(op.context,op.node)^))
        raise error("sync: operation references unobserved owner")
    if (not contains(['node','insert','delete','set','meta'],op.kind)) raise error("sync: unknown operation")
    if ((op.kind != 'insert' and op.left != null) or (not contains(['set','meta'],op.kind)^ and op.key != null) or
        (not contains(['insert','delete'],op.kind)^ and op.slot != null))
        raise error("sync: unexpected operation fields")
    if (op.kind == 'insert' and op.left != null and (not v.id(op.left)^ or not covered(op.context,op.left)^))
        raise error("sync: operation references unobserved anchor")
    if (op.kind == 'set' or op.kind == 'insert') {
        v.descriptor(op.value)^
        if (op.value.kind == 'node' and not covered(op.context,op.value.id)^)
            raise error("sync: operation references unobserved child")
    }
    if (op.kind == 'node') {
        v.fields(op.value,["kind","tag"])^
        if (not contains(['map','array','element','text','scalar'],op.value.kind)^ or
            (op.value.kind == 'element' and not (op.value.tag is symbol)) or
            (op.value.kind != 'element' and op.value.tag != null)) raise error("sync: invalid new node")
    }
    if (contains(['insert','delete'],op.kind)^ and not contains(['items','keys'],op.slot)^)
        raise error("sync: invalid sequence slot")
    if (op.kind == 'set' and not (op.key is string)) raise error("sync: invalid field key")
    if (op.kind == 'delete') {
        if (not (op.value is array) or len(op.value) == 0 or len(unique(op.value)) != len(op.value))
            raise error("sync: invalid deletion targets")
        var i = 0
        while (i < len(op.value)^) {
            if (not v.id(op.value[i])^) raise error("sync: invalid deletion ID")
            i = i+1
        }
    }
    workspace.envelope(replica,op)^
    return true
}

fn causal_time(replica, op) any^ {
    var i = 0
    while (i < len(op.context)^) {
        let entry = op.context[i]
        let previous = receipt(replica,[entry.actor,entry.counter])^
        if (previous == null or previous.time >= op.time or not dominates(op.context,previous.context)^)
            raise error("sync: invalid causal time or closure")
        i = i+1
    }
    return true
}

fn check_value(replica, owner, value) any^ {
    v.descriptor(value)^
    if (value.kind == 'node') {
        let target = node(replica, value.id)^
        if (target == null or target.parent != owner) raise error("sync: invalid child reference")
    } else if (value.kind == 'workspace') {
        if (replica.profile != 'workspace' or owner != root) raise error("sync: invalid workspace owner")
        workspace.shape(value.event)^
    } else if (value.kind != 'scalar' and value.kind != 'absent' and value.kind != 'blob') {
        raise error("sync: invalid value descriptor")
    }
    return true
}

fn check_slot(replica, owner, slot) any^ {
    let target = node(replica,owner)^
    if (target == null or not contains(['items','keys'],slot) or
        (slot == 'keys' and not contains(['map','element'],target.kind)^) or
        (slot == 'items' and not contains(['text','array','element','workspace'],target.kind)^))
        raise error("sync: invalid sequence owner or slot")
    return target
}

fn sequence_value(replica, owner, slot, value) any^ {
    let target = check_slot(replica,owner,slot)^
    check_value(replica,owner,value)^
    if (value.kind == 'absent' or
        (slot == 'keys' and (value.kind != 'scalar' or not (value.value is string))) or
        (target.kind == 'text' and (value.kind != 'scalar' or not (value.value is string) or len(value.value) != 1)) or
        (target.kind == 'workspace' and value.kind != 'workspace')) raise error("sync: invalid sequence payload")
    return true
}

fn apply_ready(replica, op) any^ {
    causal_time(replica,op)^
    workspace.ready(replica,op)^
    let owner = node(replica, op.node)^
    var nodes = replica.nodes
    var atoms = replica.atoms
    var dead = replica.dead
    var cells = replica.cells
    var meta = replica.meta
    if (op.kind == 'node') {
        v.fields(op.value,["kind","tag"])^
        if (owner == null or not contains(['map','array','element','document'],owner.kind)^ or
            not contains(['map','array','element','text','scalar'], op.value.kind))
            raise error("sync: invalid new node")
        if ((op.value.kind == 'element' and not (op.value.tag is symbol)) or
            (op.value.kind != 'element' and op.value.tag != null)) raise error("sync: invalid node tag")
        nodes = by_id([*nodes, {id:op.id, kind:op.value.kind, tag:op.value.tag, parent:op.node}])
    }
    if (op.kind == 'insert') {
        sequence_value(replica,op.node,op.slot,op.value)^
        if (op.left != null and not any([for (atom in atoms)
            atom.id == op.left and atom.node == op.node and atom.slot == op.slot]))
            raise error("sync: insertion anchor is absent")
        atoms = by_id([*atoms, {id:op.id, node:op.node, slot:op.slot,
            left:op.left, time:op.time, value:op.value}])
    }
    if (op.kind == 'delete') {
        if (owner == null or not (op.value is array)) raise error("sync: invalid deletion")
        check_slot(replica,op.node,op.slot)^
        if (len(op.value) == 0 or len(unique(op.value)) != len(op.value)) raise error("sync: invalid deletion targets")
        var index = 0
        while (index < len(op.value)^) {
            let id = op.value[index]
            if (not v.id(id)^) raise error("sync: invalid deletion ID")
            if (not any([for (atom in atoms) atom.id == id and atom.node == op.node and atom.slot == op.slot]))
                raise error("sync: deletion target is absent")
            if (not covered(op.context,id)^) raise error("sync: deletion target was not observed")
            index = index+1
        }
        dead = sort(unique([*dead, *op.value]))
    }
    if (op.kind == 'set') {
        if (owner == null or not (op.key is string)) raise error("sync: invalid field")
        if (not contains(['map','element','scalar','document'],owner.kind)^ or
            (contains(['scalar','document'],owner.kind)^ and op.key != "value"))
            raise error("sync: invalid field owner")
        check_value(replica, op.node, op.value)^
        // delayed delivery must keep assignments not observed by this writer.
        cells = assign(cells,op)^
    }
    if (op.kind == 'meta') {
        check_meta(replica,op.node,op.key,op.value)^
        meta = assign(meta,op)^
    }
    return {*:replica, time:max(replica.time, op.time), clock:clock_add(replica.clock, op.id),
        nodes:nodes, atoms:atoms, dead:dead, cells:cells,meta:meta,
        receipts:by_id([*replica.receipts, {id:op.id, hash:stamp(op)^,time:op.time,context:op.context}]), log:by_id([*replica.log, op])}
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
    if (not v.actor(actor)^) raise error("sync: actor must be nonempty text")
    if (not v.natural(replica.time+1)^ or not v.natural(next_id(replica,actor)[1])^)
        raise error("sync: counter exhausted; allocate a fresh actor or epoch")
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

pub fn set_meta(replica, actor, owner, key, value) any^ =>
    issue(replica,actor,'meta',owner,null,key,null,value)^

fn check_meta(replica, owner, key, value) any^ {
    let target = node(replica,owner)^
    if (target == null) raise error("sync: unknown metadata owner")
    if (key == "tag") {
        if (target.kind != 'element' or not (value is symbol)) raise error("sync: invalid element tag")
    } else if (key == "keys") {
        if (not contains(['map','element'],target.kind)^ or not (value is array) or
            len(unique(value)) != len(value) or not all([for (key in value) key is string]))
            raise error("sync: invalid map order")
    } else { raise error("sync: unknown metadata field") }
    return true
}

fn assign(entries, op) any^ => by_id([*[for (cell in entries where cell.node != op.node or cell.key != op.key or
    not covered(op.context,cell.id)^) cell],{id:op.id,node:op.node,key:op.key,value:op.value}])^

// RGA preorder: newer siblings precede older siblings; removed anchors stay.
pub fn walk(replica, owner, slot, left=null) any^ {
    let children = sort([for (atom in replica.atoms where atom.node == owner and
        atom.slot == slot and atom.left == left) atom], {dir:'desc', by:(atom)=>[atom.time, atom.id]});
    return [for (atom in children) for (item in [atom, *walk(replica, owner, slot, atom.id)^]) item]
}

pub fn sequence(replica, owner, slot) any^ =>
    [for (atom in walk(replica, owner, slot, null)^ where not contains(replica.dead, atom.id)^) atom]

pub fn field(replica, owner, key) => [for (cell in replica.cells where cell.node == owner and cell.key == key) cell]
pub fn metadata(replica, owner, key) => [for (cell in replica.meta where cell.node == owner and cell.key == key) cell]

fn check_forks(a, b) any^ {
    var index = 0
    while (index < len(a.receipts)^) {
        let entry = a.receipts[index]
        let other = receipt(b, entry.id)^
        if (other != null and entry != other) raise error("sync: actor operation ID reused")
        index = index+1
    }
    return true
}

fn check_immutable(a, b, atoms=false) any^ {
    var i = 0
    while (i < len(a)) {
        let entry = a[i]
        let other = lookup(b,entry.id)^
        if (other != null) {
            let x = if (atoms) {*:entry,value:null} else entry
            let y = if (atoms) {*:other,value:null} else other
            if (x != y or (atoms and entry.value != null and other.value != null and entry.value != other.value))
                raise error("sync: inconsistent immutable state")
        }
        i = i+1
    }
    return true
}

fn union_by_id(a, b) any^ => by_id([*a, *[for (entry in b where not has_id(a, entry.id)^) entry]])^

fn join_registers(a, b, ac, bc) any^ => union_by_id(
    [for (cell in a where has_id(b,cell.id)^ or not covered(bc,cell.id)^) cell],
    [for (cell in b where has_id(a,cell.id)^ or not covered(ac,cell.id)^) cell])^

pub fn merge(a, b) any^ {
    compatible(a, b)^
    check_forks(a, b)^
    check_immutable(a.nodes,b.nodes)^
    check_immutable(a.atoms,b.atoms,true)^
    check_immutable(a.cells,b.cells)^
    check_immutable(a.meta,b.meta)^
    let dead = sort(unique([*a.dead, *b.dead]))
    let floor = clock_join(a.floor, b.floor)
    // a missing dot that the other context has seen was causally overwritten.
    let cells = join_registers(a.cells,b.cells,a.clock,b.clock)^
    let atoms = [for (atom in union_by_id(a.atoms, b.atoms)^)
        if (contains(dead, atom.id) and (atom.value == null or
            any([for (other in [*a.atoms, *b.atoms]) other.id == atom.id and other.value == null])))
            {*:atom, value:null} else atom]
    let merged = {*:a, time:max(a.time,b.time), clock:clock_join(a.clock,b.clock), floor:floor,
        receipts:union_by_id(a.receipts,b.receipts), nodes:union_by_id(a.nodes,b.nodes),
        atoms:atoms, dead:dead, cells:cells,meta:join_registers(a.meta,b.meta,a.clock,b.clock)^,
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
    let checkpoint = compact(replica)
    let id = codec.digest(checkpoint)^
    return {protocol:1, id:id, digest:id, document:replica.document, epoch:replica.epoch, frontier:replica.clock, replica:checkpoint}
}

pub fn restore(saved) any^ {
    validate(saved.replica)^
    if (saved.protocol != 1 or saved.digest != codec.digest(saved.replica)^ or
        saved.document != saved.replica.document or saved.epoch != saved.replica.epoch or
        saved.frontier != saved.replica.clock) raise error("sync: corrupt saved checkpoint")
    return saved.replica
}

fn indexed(entries) any^ {
    if (not (entries is array) or entries != by_id(entries)^ or
        len(unique([for (entry in entries) entry.id])) != len(entries)) raise error("sync: invalid state index")
    return true
}

fn observed(replica, owner, context) any^ {
    if (not v.id(owner,true)^ or node(replica,owner)^ == null or
        (owner != root and not covered(context,owner)^)) raise error("sync: unobserved state node")
    return true
}

// checkpoints require host admission as well as these structural checks.
pub fn validate(replica) any^ {
    v.fields(replica,["protocol","document","profile","epoch","time","clock","floor","receipts",
        "nodes","atoms","dead","cells","meta","log","pending"])^
    if (replica.protocol != 1 or not v.actor(replica.document)^ or not contains(['text','mark','workspace'],replica.profile)^ or
        not v.natural(replica.epoch)^ or not v.natural(replica.time)^) raise error("sync: invalid replica header")
    v.clock(replica.clock)^
    v.clock(replica.floor)^
    if (not dominates(replica.clock,replica.floor)^) raise error("sync: invalid compaction floor")
    let groups = [replica.receipts,replica.nodes,replica.atoms,replica.cells,replica.meta,replica.log,replica.pending]
    var g = 0
    while (g < len(groups)) { indexed(groups[g])^; g = g+1 }
    let materialized_ids = [*[for (entry in replica.nodes where entry.id != root) entry.id],
        *[for (entry in [*replica.atoms,*replica.cells,*replica.meta]) entry.id]]
    if (len(unique(materialized_ids)) != len(materialized_ids)) raise error("sync: operation ID has multiple state kinds")
    if (not (replica.dead is array) or replica.dead != sort(unique(replica.dead))^ or len(replica.nodes) == 0)
        raise error("sync: invalid deletion index or root")
    let top = replica.nodes[0]
    if (top.id != root or top.parent != null or
        (replica.profile == 'text' and top.kind != 'text') or
        ((replica.profile == 'workspace') != (top.kind == 'workspace'))) raise error("sync: invalid root")
    if (replica.profile == 'workspace' and (len(replica.nodes) != 1 or len(replica.dead) != 0 or
        len(replica.cells) != 0 or len(replica.meta) != 0 or len(replica.atoms) != len(replica.receipts)))
        raise error("sync workspace: invalid immutable event index")
    var total = 0
    var i = 0
    while (i < len(replica.clock)^) { total = total+replica.clock[i].counter; i = i+1 }
    if (total != len(replica.receipts)) raise error("sync: incomplete causal receipts")
    let expected_time = if (len(replica.receipts) == 0) 0 else max([for (entry in replica.receipts) entry.time])
    if (replica.time != expected_time) raise error("sync: invalid replica Lamport time")
    i = 0
    while (i < len(replica.receipts)^) {
        let entry = replica.receipts[i]
        v.fields(entry,["id","hash","time","context"])^
        v.clock(entry.context)^
        if (not v.id(entry.id)^ or not covered(replica.clock,entry.id)^ or
            not (entry.hash is binary) or len(entry.hash) != 32 or not v.natural(entry.time)^ or
            entry.time <= 0 or entry.time > replica.time or not dominates(replica.clock,entry.context)^ or
            clock_get(entry.context,entry.id[0]) != entry.id[1]-1)
            raise error("sync: invalid causal receipt")
        causal_time(replica,entry)^
        i = i+1
    }
    i = 0
    while (i < len(replica.nodes)^) {
        let entry = replica.nodes[i]
        v.fields(entry,["id","kind","tag","parent"])^
        if (not contains(['map','array','element','text','scalar','document','workspace'],entry.kind)^ or
            (entry.kind == 'workspace' and entry.id != root) or
            (entry.kind == 'document' and entry.id != root) or
            (entry.kind == 'element' and not (entry.tag is symbol)) or (entry.kind != 'element' and entry.tag != null))
            raise error("sync: invalid state node")
        if (entry.id != root) {
            let evidence = receipt(replica,entry.id)^
            if (evidence == null) raise error("sync: missing node receipt")
            observed(replica,entry.parent,evidence.context)^
            if (not contains(['map','array','element','document'],node(replica,entry.parent)^.kind)^)
                raise error("sync: invalid node parent")
        }
        i = i+1
    }
    i = 0
    while (i < len(replica.atoms)^) {
        let atom = replica.atoms[i]
        v.fields(atom,["id","node","slot","left","time","value"])^
        let evidence = receipt(replica,atom.id)^
        if (evidence == null or atom.time != evidence.time) raise error("sync: missing atom receipt")
        observed(replica,atom.node,evidence.context)^
        let owner = check_slot(replica,atom.node,atom.slot)^
        if (atom.left != null) {
            let anchor = lookup(replica.atoms,atom.left)^
            if (anchor == null or anchor.node != atom.node or anchor.slot != atom.slot or not covered(evidence.context,atom.left)^)
                raise error("sync: invalid atom anchor")
        }
        if (atom.value == null) {
            if (not contains(replica.dead,atom.id)^) raise error("sync: erased live payload")
        } else {
            sequence_value(replica,atom.node,atom.slot,atom.value)^
            if (atom.value.kind == 'node' and not covered(evidence.context,atom.value.id)^)
                raise error("sync: invalid atom payload")
        }
        if (replica.profile == 'workspace') {
            let op = {protocol:replica.protocol,document:replica.document,profile:replica.profile,epoch:replica.epoch,
                id:atom.id,time:atom.time,context:evidence.context,kind:'insert',node:atom.node,slot:atom.slot,
                key:null,left:atom.left,value:atom.value}
            validate_envelope(replica,op)^
            if (stamp(op)^ != evidence.hash) raise error("sync workspace: event receipt mismatch")
            workspace.ready(replica,op)^
        }
        i = i+1
    }
    i = 0
    while (i < len(replica.dead)^) {
        if (lookup(replica.atoms,replica.dead[i])^ == null) raise error("sync: unknown deleted atom")
        i = i+1
    }
    i = 0
    while (i < len(replica.cells)^) {
        let cell = replica.cells[i]
        v.fields(cell,["id","node","key","value"])^
        let evidence = receipt(replica,cell.id)^
        if (evidence == null) raise error("sync: missing field receipt")
        observed(replica,cell.node,evidence.context)^
        let owner = node(replica,cell.node)^
        if (not (cell.key is string) or not contains(['map','element','scalar','document'],owner.kind)^ or
            (contains(['scalar','document'],owner.kind)^ and cell.key != "value")) raise error("sync: invalid state field")
        check_value(replica,cell.node,cell.value)^
        if (cell.value.kind == 'node' and not covered(evidence.context,cell.value.id)^)
            raise error("sync: unobserved field child")
        let others = field(replica,cell.node,cell.key)
        if (any([for (other in others where other.id != cell.id)
            covered(receipt(replica,other.id)^.context,cell.id)^])) raise error("sync: overwritten field retained")
        i = i+1
    }
    i = 0
    while (i < len(replica.log)^) {
        let op = replica.log[i]
        validate_envelope(replica,op)^
        if (not check_receipt(replica,op)^ or covered(replica.floor,op.id)^) raise error("sync: invalid packet log")
        i = i+1
    }
    if (len(replica.log) != sum([for (entry in replica.clock) entry.counter-clock_get(replica.floor,entry.actor)]))
        raise error("sync: uncovered update is missing")
    i = 0
    while (i < len(replica.meta)^) {
        let entry = replica.meta[i]
        v.fields(entry,["id","node","key","value"])^
        let evidence = receipt(replica,entry.id)^
        if (evidence == null) raise error("sync: missing metadata receipt")
        observed(replica,entry.node,evidence.context)^
        check_meta(replica,entry.node,entry.key,entry.value)^
        let others = metadata(replica,entry.node,entry.key)
        if (any([for (other in others where other.id != entry.id)
            covered(receipt(replica,other.id)^.context,entry.id)^])) raise error("sync: overwritten metadata retained")
        i = i+1
    }
    i = 0
    while (i < len(replica.pending)^) {
        let op = replica.pending[i]
        validate_envelope(replica,op)^
        if (check_receipt(replica,op)^ or dominates(replica.clock,op.context)^) raise error("sync: invalid pending packet")
        i = i+1
    }
    return true
}

