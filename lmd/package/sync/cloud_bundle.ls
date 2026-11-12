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
import catalog: .cloud_catalog
import maintenance: .maintenance_state
import inventory_pages: .cloud_inventory
import pages: .metadata_pages

fn context(credentials) => "lambda-cloud-bundle-1:" ++ credentials.space
fn key(credentials) any^ => crypto.mac(credentials.secret,context(credentials))^
pub fn object_key(id, epoch=0) any^ => catalog.data_key("objects",hex.hex(id)^,epoch)^
pub fn dependencies(ids, bundle_key) any^ => sort([*[for (id in ids) object_key(id,catalog.epoch(bundle_key)^)^],bundle_key])^
pub pn capture(handle, credentials, epoch=0, inventory=null, physical=null) any^ {
    let current = local.status(handle)^.state
    let replica = current.endpoint.replica
    let saved = core.snapshot(replica)^
    let frames = protocol.frames(saved.replica,workspace.model,65536)^
    let digest = frames[0].body.checkpoint
    let proof = {id:digest,kind:'checkpoint',actor:credentials.device.id,
        signature:crypto.sign(credentials.seed,auth.checkpoint_bytes(frames[0],digest)^)^}
    let ids = if (inventory == null) local.shared_inventory(handle)^ else inventory
    if (len(ids)^ > (if (physical == null) 8191 else 65536)) raise error("sync cloud: publication object limit")
    let content = {saved:saved,snapshots:current.snapshots,proof:proof,documents:documents.roots(current),
        retention:maintenance.get(current).manifest,cache:current.cache_root}
    let payload = if (physical == null) {version:3,*:content,ids:ids}
        else {version:4,*:content,inventory:physical.root}
    let bytes = crypto.seal(key(credentials)^,codec.encode(payload)^,context(credentials))^
    return {bytes:bytes,key:catalog.data_key("bundles",hex.hex(hash(bytes,'sha256')^)^,epoch)^,payload:if (physical == null) payload else {*:payload,ids:ids,locations:physical.locations}}
}
pub pn reseal(credentials, payload, root, epoch) any^ {
    let wire = {version:4,saved:payload.saved,snapshots:payload.snapshots,proof:payload.proof,
        documents:payload.documents,retention:payload.retention,cache:payload.cache,inventory:root}
    let bytes = crypto.seal(key(credentials)^,codec.encode(wire)^,context(credentials))^
    return {bytes:bytes,key:catalog.data_key("bundles",hex.hex(hash(bytes,'sha256')^)^,epoch)^,
        payload:{*:payload,inventory:root}}
}
pub fn open_payload(credentials, bytes) any^ {
    let payload = codec.decode(crypto.open(key(credentials)^,bytes,context(credentials))^)^
    v.record(payload,["version","saved","snapshots","proof"],["ids","inventory","documents","retention","cache"])^
    if (not contains([1,2,3,4],payload.version)^ or not (payload.snapshots is array) or
        (payload.version >= 2 and not (payload.documents is array)))
        raise error("sync cloud: unsupported checkpoint payload")
    core.restore(payload.saved)^
    return payload
}
pub fn inspect(credentials, control, physical_key, bytes) any^ {
    if (not ends_with(physical_key,"/" ++ hex.hex(hash(bytes,'sha256')^)^)^)
        raise error("sync audit: bundle filename mismatch")
    let payload = open_payload(credentials,bytes)^
    let frames = protocol.frames(payload.saved.replica,workspace.model,65536)^
    let signer = schema.member(control,payload.proof.actor,false)^
    if (payload.saved.replica.document != credentials.space or payload.proof.kind != 'checkpoint' or
        payload.proof.id != frames[0].body.checkpoint or
        not verify_signature(signer.public_key,auth.checkpoint_bytes(frames[0],payload.proof.id)^,
            payload.proof.signature,'ed25519')^)
        raise error("sync audit: invalid checkpoint proof")
    return true
}
pub fn decode(credentials, publication, bytes) any^ {
    if (not ends_with(publication.bundle.key,"/" ++ hex.hex(hash(bytes,'sha256')^)^)^ or
        len(binary(bytes)^) != publication.bundle.bytes) raise error("sync cloud: bundle digest mismatch")
    let payload = open_payload(credentials,bytes)^
    let replica = core.restore(payload.saved)^
    if (payload.version == 4) {
        if (payload.inventory != null) { pages.valid_ref(payload.inventory)^ }
        if (publication.version != 2 or replica.document != credentials.space or replica.clock != publication.clock or
            payload.proof.actor != publication.actor or schema.digest(payload.inventory)^ != publication.inventory)
            raise error("sync cloud: invalid paged checkpoint bundle")
        return payload
    }
    if (not contains([1,2,3],payload.version)^ or (payload.version >= 2 and not (payload.documents is array)) or
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
    if (payload.retention != null and not contains(payload.ids,payload.retention)^)
        raise error("sync cloud: missing retirement manifest")
    if (payload.cache != null and not contains(payload.ids,payload.cache)^)
        raise error("sync cloud: missing availability manifest")
    return payload
}
pub pn expand(config, credentials, publication, payload) any^ {
    if (payload.version != 4) {
        return {*:payload,locations:[for (id in payload.ids) {id:id,key:object_key(id,catalog.epoch(publication.bundle.key)^)^}]}
    }
    inventory_pages.verify_binding(config,credentials,publication,payload.inventory)^
    let inventory = inventory_pages.expand(config,credentials,payload.inventory)^
    let ids = inventory.ids
    if (not (payload.snapshots is array) or not all([for (id in payload.snapshots) contains(ids,id)^]) or
        not (payload.documents is array) or len(payload.documents)^ > 64 or
        not all([for (root in payload.documents) contains(ids,root.object)^]) or
        (payload.retention != null and not contains(ids,payload.retention)^) or
        (payload.cache != null and not contains(ids,payload.cache)^))
        raise error("sync cloud: paged inventory omits a checkpoint dependency")
    return {*:payload,ids:ids,locations:inventory.locations}
}
pub pn install_checkpoint(handle, credentials, device, payload) any^ {
    local.register_retention(handle,payload.retention)^
    local.register_cache(handle,payload.cache)^
    let frames = protocol.frames(payload.saved.replica,workspace.model,65536)^
    for (frame in frames) { exchange.receive_frame(handle,credentials,device,{packet:frame,proof:payload.proof})^ }
    local.register_snapshots(handle,payload.snapshots)^
    local.register_documents(handle,if (payload.documents == null) [] else payload.documents)^
    local.release_staging(handle,payload.ids)^
    return true
}
