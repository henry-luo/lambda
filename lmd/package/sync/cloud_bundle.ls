// Checkpoint bytes remain encrypted in object storage; signed publication binds their digest.
import crypto: lambda.io.crypto
import hex: lambda.security.tuf.metadata
import codec: .codec
import core: .core
import protocol: .protocol
import local: .local
import auth: .auth
import workspace: .workspace
import schema: .cloud_schema
import exchange: .peer_exchange
import v: .schema
import documents: .documents

fn context(credentials) => "lambda-cloud-bundle-1:" ++ credentials.space
fn key(credentials) any^ => crypto.mac(credentials.secret,context(credentials))^
pub fn object_key(id) any^ => "objects/" ++ hex.hex(id)^
pub fn dependencies(ids, bundle_key) any^ => sort([*[for (id in ids) object_key(id)^],bundle_key])^
pub pn capture(handle, credentials) any^ {
    let current = local.status(handle)^.state
    let replica = current.endpoint.replica
    let saved = core.snapshot(replica)^
    let frames = protocol.frames(saved.replica,workspace.model,65536)^
    let digest = frames[0].body.checkpoint
    let proof = {id:digest,kind:'checkpoint',actor:credentials.device.id,
        signature:crypto.sign(credentials.seed,auth.checkpoint_bytes(frames[0],digest)^)^}
    let ids = local.inventory(handle)^
    if (len(ids) > 8191) raise error("sync cloud: publication object limit")
    let payload = {version:2,saved:saved,ids:ids,snapshots:current.snapshots,proof:proof,documents:documents.roots(current)}
    let bytes = crypto.seal(key(credentials)^,codec.encode(payload)^,context(credentials))^
    return {bytes:bytes,key:"bundles/" ++ hex.hex(hash(bytes,'sha256')^)^,payload:payload}
}
pub fn decode(credentials, publication, bytes) any^ {
    if (publication.bundle.key != "bundles/" ++ hex.hex(hash(bytes,'sha256')^)^ or
        len(binary(bytes)^) != publication.bundle.bytes) raise error("sync cloud: bundle digest mismatch")
    let payload = codec.decode(crypto.open(key(credentials)^,bytes,context(credentials))^)^
    v.record(payload,["version","saved","ids","snapshots","proof"],["documents"])^
    let replica = core.restore(payload.saved)^
    if (not contains([1,2],payload.version)^ or (payload.version == 2 and not (payload.documents is array)) or
        replica.document != credentials.space or replica.clock != publication.clock or
        not (payload.ids is array) or len(payload.ids) > 8191 or payload.proof.actor != publication.actor or schema.digest(payload.ids)^ != publication.inventory or
        len(unique(payload.ids)) != len(payload.ids)) raise error("sync cloud: invalid checkpoint bundle")
    for (id in payload.ids) { exchange.object_id(id)^ }
    if (not (payload.snapshots is array) or not all([for (id in payload.snapshots) contains(payload.ids,id)^]))
        raise error("sync cloud: missing named snapshot")
    if (payload.documents != null and (len(payload.documents)^ > 64 or
        not all([for (root in payload.documents) contains(payload.ids,root.object)^])))
        raise error("sync cloud: missing document state")
    if (schema.dependency_digest(dependencies(payload.ids,publication.bundle.key)^)^ != publication.dependencies)
        raise error("sync cloud: checkpoint dependency commitment mismatch")
    return payload
}
pub pn install_checkpoint(handle, credentials, device, payload) any^ {
    let frames = protocol.frames(payload.saved.replica,workspace.model,65536)^
    for (frame in frames) { exchange.receive_frame(handle,credentials,device,{packet:frame,proof:payload.proof})^ }
    local.register_snapshots(handle,payload.snapshots)^
    local.register_documents(handle,if (payload.documents == null) [] else payload.documents)^
    return true
}
