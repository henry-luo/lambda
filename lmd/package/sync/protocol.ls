// transport-independent anti-entropy; hosts authenticate before supplying admission.
import c: .core
import v: .schema
import codec: .codec
import blob: .blob
import change: .change

pub let default_schema = ["mark",1]

fn schema(value, profile=null) any^ {
    if (not (value is array) or len(value) != 2 or not v.actor(value[0])^ or
        not v.natural(value[1])^ or value[1] == 0) raise error("sync protocol: invalid schema")
    if (profile == 'workspace' and value != ["workspace",1]) raise error("sync protocol: workspace schema mismatch")
    return true
}

fn message(replica, model, kind, body) => {protocol:"lambda-crdt",version:1,codec:codec.version,
    document:replica.document,profile:replica.profile,epoch:replica.epoch,schema:model,
    root:replica.nodes[0],kind:kind,body:body}

fn header(replica, model, packet, same_epoch=true) any^ {
    v.fields(packet,["protocol","version","codec","document","profile","epoch","schema","root","kind","body"])^
    schema(packet.schema,replica.profile)^
    if (packet.protocol != "lambda-crdt" or packet.version != 1 or packet.codec != codec.version or
        packet.document != replica.document or packet.profile != replica.profile or packet.schema != model or
        packet.root != replica.nodes[0] or not v.natural(packet.epoch)^ or (same_epoch and packet.epoch != replica.epoch))
        raise error("sync protocol: incompatible version, document, profile, schema, root or epoch")
    return true
}

pub fn have(replica, model=default_schema) any^ {
    schema(model,replica.profile)^
    return message(replica,model,'have',{clock:replica.clock,floor:replica.floor})
}

fn have_body(packet) any^ {
    if (packet.kind != 'have') raise error("sync protocol: expected HAVE")
    v.fields(packet.body,["clock","floor"])^
    v.clock(packet.body.clock)^
    v.clock(packet.body.floor)^
    if (not c.dominates(packet.body.clock,packet.body.floor)^) raise error("sync protocol: invalid HAVE floor")
    return true
}

pub fn checkpoint(replica) => {*:c.compact(replica),pending:[]}

pub fn reply(replica, peer_have, model=default_schema, max_ops=128, max_bytes=65536) any^ {
    header(replica,model,peer_have,false)^
    have_body(peer_have)^
    if (not v.natural(max_ops)^ or max_ops == 0 or max_ops > 1024 or not v.natural(max_bytes)^ or max_bytes < 1024)
        raise error("sync protocol: invalid batch limits")
    if (peer_have.epoch != replica.epoch) {
        return message(replica,model,'bootstrap_required',{checkpoint:codec.digest(checkpoint(replica))^})
    }
    if (not c.dominates(peer_have.body.clock,replica.floor)^) {
        let compacted = checkpoint(replica)
        return message(replica,model,'checkpoint_required',{checkpoint:codec.digest(compacted)^,
            bytes:len(binary(codec.encode(compacted)^))^,frontier:replica.clock})
    }
    let missing = sort([for (op in replica.log where not c.covered(peer_have.body.clock,op.id)^) op],
        (op)=>[op.time,op.id])^
    var selected = []
    var i = 0
    while (i < len(missing) and i < max_ops) {
        let candidate = [*selected,missing[i]]
        let packet = message(replica,model,'updates',{clock:replica.clock,operations:candidate,more:i+1<len(missing)})
        if (len(binary(codec.encode(packet)^))^ > max_bytes) {
            if (i == 0) raise error("sync protocol: operation exceeds batch limit")
            return message(replica,model,'updates',{clock:replica.clock,operations:selected,more:true})
        }
        selected = candidate
        i = i+1
    }
    return message(replica,model,'updates',{clock:replica.clock,operations:selected,more:i<len(missing)})
}

// immutable frames can be retried in any order; size includes the typed envelope.
pub fn frames(replica, model=default_schema, max_bytes=65536, expected=null) any^ {
    if (not v.natural(max_bytes)^ or max_bytes < 2048) raise error("sync protocol: invalid frame limit")
    let payload = codec.encode(checkpoint(replica))^
    let digest = hash(payload,'sha256')^
    if (expected != null and expected != digest) raise error("sync protocol: checkpoint changed; restart negotiation")
    var width = int(max_bytes/8)^
    var result = []
    var complete = false
    while (not complete) {
        let total = int(ceil(len(payload)/width))
        result = [for (i in 0 to (total-1)) message(replica,model,'checkpoint_frame',
            {checkpoint:digest,index:i,total:total,text:slice(payload,i*width,(i+1)*width)^})]
        if (all([for (packet in result) len(binary(codec.encode(packet)^))^ <= max_bytes])) { complete = true }
        else {
            width = int(width/2)
            if (width == 0) raise error("sync protocol: frame header exceeds limit")
        }
    }
    return result
}

pub fn start(replica, model=default_schema) any^ {
    c.validate(replica)^
    schema(model,replica.profile)^
    return {replica:replica,schema:model,transfers:[],transactions:[],journal:[],received:[],recoveries:[],local:[],cloud:[],peers:[]}
}

fn admitted(admission) any^ {
    v.fields(admission,["actors","checkpoints"])^
    if (not (admission.actors is array) or not (admission.checkpoints is array))
        raise error("sync protocol: invalid host admission")
    return true
}

fn bounded(endpoint, max_bytes) any^ {
    if (len(endpoint.replica.pending) > 4096 or len(endpoint.transactions) > 64 or len(endpoint.transfers) > 2 or
        len(binary(codec.encode([endpoint.replica.pending,endpoint.transactions,endpoint.journal])^))^ +
        sum([for (transfer in endpoint.transfers) transfer.bytes]) > max_bytes)
        raise error("sync protocol: pending data limit; checkpoint recovery required")
    return endpoint
}

fn receive_frame(endpoint, packet, admission, max_bytes) any^ {
    let body = packet.body
    v.fields(body,["checkpoint","index","total","text"])^
    if (not contains(admission.checkpoints,body.checkpoint)^) raise error("sync protocol: checkpoint is not admitted")
    if (not (body.checkpoint is binary) or len(body.checkpoint) != 32 or
        not v.natural(body.index)^ or not v.natural(body.total)^ or body.total == 0 or body.total > 8192 or
        body.index >= body.total or not (body.text is string)) raise error("sync protocol: invalid checkpoint frame")
    if (contains(endpoint.received,body.checkpoint)^) { return endpoint }
    let old = c.lookup(endpoint.transfers,body.checkpoint)^
    if (old == null and len(endpoint.transfers) >= 2) raise error("sync protocol: checkpoint transfer limit")
    let transfer = if (old == null) {id:body.checkpoint,total:body.total,parts:[],bytes:0} else old
    if (transfer.total != body.total) raise error("sync protocol: inconsistent checkpoint frame count")
    let previous = c.lookup(transfer.parts,body.index)^
    if (previous != null and previous.text != body.text) raise error("sync protocol: inconsistent checkpoint retry")
    let parts = if (previous == null) [*transfer.parts,{id:body.index,text:body.text}] else transfer.parts
    let bytes = transfer.bytes + (if (previous == null) len(binary(body.text))^ else 0)
    if (bytes > max_bytes) raise error("sync protocol: checkpoint size limit")
    let others = [for (item in endpoint.transfers where item.id != body.checkpoint) item]
    if (len(parts) != body.total) {
        return {*:endpoint,transfers:sort([*others,{*:transfer,parts:sort(parts,(p)=>p.id)^,bytes:bytes}],(t)=>t.id)^}
    }
    let payload = join([for (part in sort(parts,(p)=>p.id)^) part.text],"")^
    if (hash(payload,'sha256')^ != body.checkpoint) raise error("sync protocol: checkpoint digest mismatch")
    let replica = codec.decode(payload,max_bytes)^
    c.validate(replica)^
    header(replica,endpoint.schema,packet)^
    return {*:endpoint,replica:c.merge(endpoint.replica,replica)^,transfers:others,
        received:take([body.checkpoint,*endpoint.received],16)}
}

fn authorize(operations, admission) any^ {
    if (not (operations is array) or len(operations) > 1024) raise error("sync protocol: invalid update batch")
    var i = 0
    while (i < len(operations)) {
        let op = operations[i]
        if (not contains(admission.actors,op.id[0])^) raise error("sync protocol: actor is not admitted")
        i = i+1
    }
    return true
}

fn drain_transactions(endpoint, admission) any^ {
    var replica = endpoint.replica
    var journal = endpoint.journal
    var waiting = endpoint.transactions
    var progress = true
    while (progress) {
        progress = false
        var rest = []
        var i = 0
        while (i < len(waiting)^) {
            let body = waiting[i]
            authorize(body.operations,admission)^
            let candidate = c.receive(replica,body.operations)^
            if (c.dominates(candidate.clock,body.frontier)^) {
                replica = candidate
                if (c.lookup(journal,body.id)^ == null and not c.dominates(replica.floor,body.frontier)^) {
                    journal = sort([*journal,body],(tx)=>tx.id)^
                }
                progress = true
            }
            else { rest = [*rest,body] }
            i = i+1
        }
        waiting = rest
    }
    return {*:endpoint,replica:replica,transactions:waiting,journal:journal}
}

fn check_transaction(replica, body) any^ {
    v.fields(body,["id","base","frontier","operations"])^
    v.clock(body.base)^
    v.clock(body.frontier)^
    if (not (body.operations is array) or len(body.operations) > 1024) raise error("sync protocol: invalid transaction operations")
    let expected = codec.digest([replica.document,replica.profile,replica.epoch,
        {base:body.base,frontier:body.frontier,operations:body.operations}])^
    let frontier = c.clock_join(body.base,[for (actor in unique([for (op in body.operations) op.id[0]]))
        {actor:actor,counter:max([for (op in body.operations where op.id[0] == actor) op.id[1]])}])
    if (body.id != expected or body.frontier != frontier) raise error("sync protocol: invalid transaction frontier or digest")
    let difference = sum([for (entry in frontier) entry.counter-c.clock_get(body.base,entry.actor)])
    if (difference != len(body.operations) or len(unique([for (op in body.operations) op.id])) != len(body.operations) or
        not all([for (op in body.operations) not c.covered(body.base,op.id)^ and c.dominates(frontier,op.context)^]))
        raise error("sync protocol: incomplete transaction operation range")
    c.receive(replica,body.operations)^
    return true
}

fn receive_transaction(endpoint, packet, admission, max_bytes) any^ {
    let body = packet.body
    check_transaction(endpoint.replica,body)^
    authorize(body.operations,admission)^
    let old = c.lookup(endpoint.transactions,body.id)^
    let waiting = if (old == null) sort([*endpoint.transactions,body],(tx)=>tx.id)^ else endpoint.transactions
    if (len(waiting) > 64 or len(binary(codec.encode(waiting)^))^ > max_bytes)
        raise error("sync protocol: pending transaction limit")
    return drain_transactions({*:endpoint,transactions:waiting},admission)^
}

pub fn transaction(before, after, model=default_schema, max_bytes=8388608) any^ {
    let packet = message(after,model,'transaction',change.batch(before,after)^)
    if (len(binary(codec.encode(packet)^))^ > max_bytes) raise error("sync protocol: transaction size limit")
    return packet
}

pub fn accept(endpoint, encoded, admission, max_bytes=8388608) any^ {
    admitted(admission)^
    let packet = codec.decode(encoded,max_bytes)^
    header(endpoint.replica,endpoint.schema,packet)^
    if (packet.kind == 'checkpoint_frame') { return bounded(drain_transactions(receive_frame(endpoint,packet,admission,max_bytes)^,admission)^,max_bytes)^ }
    if (packet.kind == 'transaction') { return bounded(receive_transaction(endpoint,packet,admission,max_bytes)^,max_bytes)^ }
    if (packet.kind != 'updates') raise error("sync protocol: expected updates or checkpoint frame")
    v.fields(packet.body,["clock","operations","more"])^
    v.clock(packet.body.clock)^
    authorize(packet.body.operations,admission)^
    if (not (packet.body.more is bool))
        raise error("sync protocol: invalid update batch")
    var i = 0
    while (i < len(packet.body.operations)^) {
        let op = packet.body.operations[i]
        if (not c.covered(packet.body.clock,op.id)^ or not c.dominates(packet.body.clock,op.context)^)
            raise error("sync protocol: invalid advertised frontier")
        i = i+1
    }
    return bounded(drain_transactions({*:endpoint,replica:c.receive(endpoint.replica,packet.body.operations)^},admission)^,max_bytes)^
}

pub fn cancel_transfer(endpoint, digest) => {*:endpoint,
    transfers:[for (item in endpoint.transfers where item.id != digest) item]}

pub fn local_change(endpoint, actor, commands, max_bytes=8388608) any^ {
    let edited = change.transaction(endpoint.replica,actor,commands)^
    if (len(edited.batch.operations) > 1024) raise error("sync protocol: transaction operation limit")
    let journal = if (len(edited.batch.operations) == 0) endpoint.journal else
        sort([*endpoint.journal,edited.batch],(tx)=>tx.id)^
    return {endpoint:bounded({*:endpoint,replica:edited.replica,journal:journal},max_bytes)^,undo:edited.undo}
}

// endpoints relay the original atomic groups until a checkpoint covers them.
pub fn next(endpoint, peer_have, max_ops=128, max_bytes=65536) any^ {
    let candidate = reply(endpoint.replica,peer_have,endpoint.schema,max_ops,max_bytes)^
    if (candidate.kind != 'updates' or len(candidate.body.operations) == 0) { return candidate }
    let groups = [for (tx in endpoint.journal where c.dominates(endpoint.replica.clock,tx.frontier)^) tx]
    let first_id = candidate.body.operations[0].id
    let group = [for (tx in groups where any([for (op in tx.operations) op.id == first_id])) tx]
    if (len(group) > 0) {
        let packet = message(endpoint.replica,endpoint.schema,'transaction',group[0])
        if (len(binary(codec.encode(packet)^))^ > max_bytes) raise error("sync protocol: atomic transaction exceeds batch limit")
        return packet
    }
    var selected = []
    var i = 0
    var stop = false
    while (i < len(candidate.body.operations)^ and not stop) {
        let op = candidate.body.operations[i]
        stop = any([for (tx in groups) any([for (member in tx.operations) member.id == op.id])])
        if (not stop) { selected = [*selected,op] }
        i = i+1
    }
    return {*:candidate,body:{*:candidate.body,operations:selected,more:stop or candidate.body.more}}
}

pub fn compact(endpoint) any^ => {*:endpoint,replica:c.compact(endpoint.replica),
    journal:[for (tx in endpoint.journal where not c.dominates(endpoint.replica.clock,tx.frontier)^) tx]}

// caller presents an already authenticated current-epoch checkpoint. Old work is preserved.
pub fn bootstrap(endpoint, replica, admitted_digest) any^ {
    c.validate(replica)^
    if (replica.document != endpoint.replica.document or replica.profile != endpoint.replica.profile or
        replica.nodes[0] != endpoint.replica.nodes[0] or
        replica.epoch <= endpoint.replica.epoch or codec.digest(replica)^ != admitted_digest)
        raise error("sync protocol: invalid epoch bootstrap")
    return {*:endpoint,replica:replica,transfers:[],transactions:[],journal:[],received:[],local:[],cloud:[],peers:[],
        recoveries:[*endpoint.recoveries,{epoch:endpoint.replica.epoch,replica:endpoint.replica,
            transactions:endpoint.transactions,journal:endpoint.journal,transfers:endpoint.transfers}]}
}

// receipt acknowledgments are transport messages; durable evidence uses record_durable.
pub fn ack(endpoint) any^ => message(endpoint.replica,endpoint.schema,'ack',{clock:endpoint.replica.clock,level:'received'})^

pub fn accept_ack(endpoint, encoded, peer) any^ {
    let packet = codec.decode(encoded)^
    header(endpoint.replica,endpoint.schema,packet)^
    v.fields(packet.body,["clock","level"])^
    if (packet.kind != 'ack' or packet.body.level != 'received') raise error("sync protocol: invalid receipt acknowledgment")
    return acknowledge(endpoint,peer,packet.body.clock)^
}

pub fn replace_replica(endpoint, replica) any^ => {*:endpoint,replica:c.merge(endpoint.replica,replica)^}

// a locally authored offline/import branch has no transfer or waiting groups;
// preserve its atomic boundaries when joining it into the active endpoint.
pub fn join_branch(endpoint, branch) any^ {
    validate_endpoint(endpoint)^
    validate_endpoint(branch)^
    if (endpoint.schema != branch.schema or len(branch.transactions)^ != 0 or
        len(branch.transfers)^ != 0 or len(branch.recoveries)^ != 0) raise error("sync protocol: invalid authored branch")
    let replica = c.merge(endpoint.replica,branch.replica)^
    var journal = endpoint.journal
    var i = 0
    while (i < len(branch.journal)^) {
        let group = branch.journal[i]
        let previous = c.lookup(journal,group.id)^
        if (previous != null and previous != group) raise error("sync protocol: inconsistent atomic group")
        if (previous == null) { journal = [*journal,group] }
        i = i+1
    }
    return {*:endpoint,replica:replica,journal:sort([for (group in journal where
        not c.dominates(replica.floor,group.frontier)^) group],(group)=>group.id)^}
}

pub fn quarantine(endpoint, digest, reason) any^ {
    let batch = c.lookup(endpoint.transactions,digest)^
    if (batch == null or not (reason is string)) raise error("sync protocol: unknown pending transaction")
    return {*:endpoint,transactions:[for (tx in endpoint.transactions where tx.id != digest) tx],
        recoveries:[*endpoint.recoveries,{epoch:endpoint.replica.epoch,replica:endpoint.replica,
            transactions:[batch],journal:[],transfers:[],reason:reason}]}
}

fn replica_has_actor(replica, actor) any^ => c.clock_get(replica.clock,actor) != 0 or
    any([for (op in replica.pending) op.id[0] == actor])

pub fn fresh_writer(endpoint, actor) any^ => v.actor(actor)^ and
    not replica_has_actor(endpoint.replica,actor)^ and
    not any([for (tx in endpoint.transactions) for (op in tx.operations) op.id[0] == actor]) and
    not any([for (old in endpoint.recoveries) replica_has_actor(old.replica,actor)^ or
        any([for (tx in old.transactions) for (op in tx.operations) op.id[0] == actor])])

pub fn validate_endpoint(endpoint, max_bytes=8388608) any^ {
    v.fields(endpoint,["replica","schema","transfers","transactions","journal","received","recoveries","local","cloud","peers"])^
    c.validate(endpoint.replica)^
    schema(endpoint.schema,endpoint.replica.profile)^
    validate_frontier(endpoint.replica,endpoint.local)^
    validate_frontier(endpoint.replica,endpoint.cloud)^
    let queues = [endpoint.transfers,endpoint.transactions,endpoint.journal,endpoint.received,endpoint.recoveries,endpoint.peers]
    if (not all([for (queue in queues) queue is array]) or len(endpoint.received) > 16)
        raise error("sync protocol: invalid endpoint queues")
    var i = 0
    while (i < len(endpoint.peers)^) {
        let peer = endpoint.peers[i]
        v.fields(peer,["id","clock"])^
        if (not v.actor(peer.id)^) raise error("sync protocol: invalid peer")
        validate_frontier(endpoint.replica,peer.clock)^
        i = i+1
    }
    if (len(unique([for (peer in endpoint.peers) peer.id])) != len(endpoint.peers)) raise error("sync protocol: duplicate peer")
    i = 0
    while (i < len(endpoint.received)^) {
        let digest = endpoint.received[i]
        if (not (digest is binary) or len(digest)^ != 32) raise error("sync protocol: invalid received checkpoint")
        i = i+1
    }
    i = 0
    while (i < len(endpoint.transfers)^) {
        let transfer = endpoint.transfers[i]
        v.fields(transfer,["id","total","parts","bytes"])^
        if (not (transfer.id is binary) or len(transfer.id)^ != 32 or not v.natural(transfer.total)^ or
            transfer.total == 0 or transfer.total > 8192 or not (transfer.parts is array) or
            len(transfer.parts)^ >= transfer.total) raise error("sync protocol: invalid stored transfer")
        var j = 0
        var bytes = 0
        while (j < len(transfer.parts)^) {
            let part = transfer.parts[j]
            v.fields(part,["id","text"])^
            if (not v.natural(part.id)^ or part.id >= transfer.total or not (part.text is string))
                raise error("sync protocol: invalid stored frame")
            bytes = bytes+len(binary(part.text))^
            j = j+1
        }
        if (bytes != transfer.bytes or len(unique([for (part in transfer.parts) part.id])) != len(transfer.parts)^)
            raise error("sync protocol: invalid stored transfer accounting")
        i = i+1
    }
    i = 0
    while (i < len(endpoint.transactions)^) { check_transaction(endpoint.replica,endpoint.transactions[i])^; i = i+1 }
    i = 0
    while (i < len(endpoint.journal)^) {
        let tx = endpoint.journal[i]
        check_transaction(endpoint.replica,tx)^
        if (not c.dominates(endpoint.replica.clock,tx.frontier)^) raise error("sync protocol: unapplied journal transaction")
        i = i+1
    }
    i = 0
    while (i < len(endpoint.recoveries)^) {
        let old = endpoint.recoveries[i]
        c.validate(old.replica)^
        if (old.epoch != old.replica.epoch or old.replica.document != endpoint.replica.document or
            old.epoch > endpoint.replica.epoch) raise error("sync protocol: invalid recovery branch")
        i = i+1
    }
    bounded(endpoint,max_bytes)^
    return true
}

pub fn validate_frontier(replica, frontier) any^ {
    v.clock(frontier)^
    if (not c.dominates(replica.clock,frontier)^) raise error("sync protocol: acknowledgment exceeds accepted state")
    var i = 0
    while (i < len(frontier)) {
        let entry = frontier[i]
        let proof = c.lookup(replica.receipts,[entry.actor,entry.counter])^
        if (proof == null or not c.dominates(frontier,proof.context)^) raise error("sync protocol: acknowledgment is not causally closed")
        i = i+1
    }
    return true
}

pub fn acknowledge(endpoint, peer, frontier) any^ {
    validate_frontier(endpoint.replica,frontier)^
    if (not v.actor(peer)^) raise error("sync protocol: invalid peer identity")
    let old = c.lookup(endpoint.peers,peer)^
    let clock = if (old == null) frontier else c.clock_join(old.clock,frontier)
    return {*:endpoint,peers:[*[for (p in endpoint.peers where p.id != peer) p],{id:peer,clock:clock}]}
}

// this call records host evidence after durable publication; receiving packets never calls it.
pub fn record_durable(endpoint, level, captured, available=[]) any^ {
    let restored = c.restore(captured)^
    validate_frontier(endpoint.replica,restored.clock)^
    c.merge(endpoint.replica,restored)^
    if (len(blob.missing(restored,available)^) != 0) raise error("sync protocol: referenced blobs are not durable")
    let frontier = restored.clock
    if (level == 'local') { return {*:endpoint,local:c.clock_join(endpoint.local,frontier)} }
    if (level == 'cloud') { return {*:endpoint,cloud:c.clock_join(endpoint.cloud,frontier)} }
    raise error("sync protocol: invalid durability level")
}

pub fn durable(endpoint, level, frontier, available=[]) any^ {
    if (frontier != endpoint.replica.clock) raise error("sync protocol: use a captured checkpoint for an older acknowledgment")
    return record_durable(endpoint,level,c.snapshot(endpoint.replica)^,available)^
}

pub fn status(endpoint) => {local:c.dominates(endpoint.local,endpoint.replica.clock),
    peer:any([for (peer in endpoint.peers) c.dominates(peer.clock,endpoint.replica.clock)]),
    cloud:c.dominates(endpoint.cloud,endpoint.replica.clock),pending:len(endpoint.replica.pending)+len(endpoint.transactions)}
