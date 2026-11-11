// signatures are native primitives; grant selection and signed domains are pure.
import c: .core
import v: .schema
import codec: .codec

pub fn operation_bytes(op) any^ => codec.encode(["lambda-crdt-operation-1",c.stamp(op)^])^
pub fn transaction_bytes(packet) any^ => codec.encode(["lambda-crdt-transaction-1",
    packet.document,packet.profile,packet.epoch,packet.schema,packet.root,packet.body])^
pub fn checkpoint_bytes(packet, digest) any^ => codec.encode(["lambda-crdt-checkpoint-1",
    packet.document,packet.profile,packet.epoch,packet.schema,packet.root,digest])^

fn grant(grants, actor, packet, permission) any^ {
    let matches = [for (entry in grants where entry.actor == actor and entry.document == packet.document and
        entry.profile == packet.profile and entry.epoch == packet.epoch and entry.schema == packet.schema and
        contains(entry.permissions,permission)^) entry]
    if (len(matches) != 1) raise error("sync auth: missing or ambiguous grant")
    let key = matches[0].public_key
    if (not (key is binary) or len(key) != 32) raise error("sync auth: invalid Ed25519 public key")
    return key
}

fn verify(proofs, id, kind, payload, packet, grants, permission) any^ {
    let proof = c.lookup(proofs,id)^
    if (proof == null) raise error("sync auth: missing proof")
    v.fields(proof,["id","kind","actor","signature"])^
    if (proof.kind != kind or not v.actor(proof.actor)^ or not (proof.signature is binary) or len(proof.signature) != 64)
        raise error("sync auth: invalid proof")
    let key = grant(grants,proof.actor,packet,permission)^
    if (not verify_signature(key,payload,proof.signature,'ed25519')^) raise error("sync auth: invalid signature")
    return proof.actor
}

// hosts supply current grants; encrypted transports/repositories carry proofs with packets.
pub fn admission(packet, proofs, grants) any^ {
    if (packet.kind == 'checkpoint_frame') {
        verify(proofs,packet.body.checkpoint,'checkpoint',checkpoint_bytes(packet,packet.body.checkpoint)^,
            packet,grants,'checkpoint')^
        return {actors:[],checkpoints:[packet.body.checkpoint]}
    }
    if (packet.kind == 'transaction') {
        let actor = verify(proofs,packet.body.id,'transaction',transaction_bytes(packet)^,packet,grants,'write')^
        if (not all([for (op in packet.body.operations) op.id[0] == actor]))
            raise error("sync auth: transaction has another author's operations")
        return {actors:[actor],checkpoints:[]}
    }
    if (packet.kind != 'updates') raise error("sync auth: expected data packet")
    var actors = []
    var i = 0
    while (i < len(packet.body.operations)^) {
        let op = packet.body.operations[i]
        let actor = verify(proofs,op.id,'operation',operation_bytes(op)^,packet,grants,'write')^
        if (actor != op.id[0]) raise error("sync auth: operation author mismatch")
        actors = unique([*actors,actor])
        i = i+1
    }
    return {actors:actors,checkpoints:[]}
}
