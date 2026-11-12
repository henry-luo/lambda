import local: lambda.sync.local
import host: lambda.sync.document_host
import model: lambda.sync.document_state
import exchange: lambda.sync.document_exchange
import identity: lambda.sync.peer_identity
import bundle: lambda.sync.cloud_bundle
import transport: lambda.sync.cloud_transport
import cloud_schema: lambda.sync.cloud_schema
import crypto: lambda.io.crypto
import blob: lambda.sync.blob
import codec: lambda.sync.codec
import helpers: ~~.mod_sync_local_helpers
import c: lambda.sync.core
import p: lambda.sync.protocol
import mark: lambda.sync.mark

pn main() {
    let area = "temp/sync-document-admission-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/a")^
    io.mkdir(area ++ "/b")^
    let credentials = identity.create("document-admission",crypto.key()^)^
    let recipient = identity.create(credentials.space,credentials.secret)^
    let a = local.create(area ++ "/a.sqlite",credentials.secret,credentials.space,area ++ "/a","a")^
    let b = local.create(area ++ "/b.sqlite",credentials.secret,credentials.space,area ++ "/b","b")^
    output("{\"count\":1}",area ++ "/a/data.json")^
    let opened = host.open_document(a,"data.json",'mark','json')^
    let initial = model.get(local.status(a)^.state,opened.id)^
    helpers.transfer(a,b)^
    local.register_documents(b,[{id:initial.id,object:initial.export_id}])^
    local.materialize(b)^
    let before = model.get(local.status(b)^.state,opened.id)^
    let attachment = host.attachment(a,b'0001FEFF')^
    host.apply_edits(a,opened.id,[{kind:'blob',key:"asset",value:attachment}])^
    let lossy_json = host.prepare_save(a,opened.id) ^ { ^ }
    let changed = model.get(local.status(a)^.state,opened.id)^
    let envelope = exchange.reply(changed,credentials,p.have(before.endpoint.replica)^)^
    let wrong = exchange.receive_packet(b,recipient,recipient.device,envelope) ^ { ^ }
    let tampered = exchange.receive_packet(b,recipient,credentials.device,
        {*:envelope,body:{*:envelope.body,origin:codec.digest("wrong")^}}) ^ { ^ }
    let missing = exchange.receive_packet(b,recipient,credentials.device,envelope) ^ { ^ }
    let rolled_back = model.get(local.status(b)^.state,opened.id)^
    local.import_objects(b,local.export_objects(a,local.inventory(a)^)^)^
    let received = exchange.receive_packet(b,recipient,credentials.device,envelope)^
    let duplicate = exchange.receive_packet(b,recipient,credentials.device,envelope)^
    let delivered = model.get(local.status(b)^.state,opened.id)^
    let exact = local.read_content(b,attachment)^
    host.compact(a,opened.id)^
    let compacted = model.get(local.status(a)^.state,opened.id)^
    let captured = bundle.capture(a,credentials)^
    let descriptor = transport.descriptor(captured.key,captured.bytes)^
    let publication = {bundle:descriptor,actor:credentials.device.id,clock:captured.payload.saved.frontier,
        inventory:cloud_schema.digest(captured.payload.ids)^,
        dependencies:cloud_schema.dependency_digest(bundle.dependencies(captured.payload.ids,captured.key)^)^}
    let payload = bundle.decode(recipient,publication,captured.bytes)^
    let initial_cloud = p.status(delivered.endpoint)^.cloud
    bundle.install_checkpoint(b,recipient,credentials.device,payload)^
    local.record_cloud(b,"provider",credentials.device.id,1,"publication",payload.saved,payload.ids,payload.documents)^
    let acknowledged = model.get(local.status(b)^.state,opened.id)^
    output("{ invalid",area ++ "/b/data.json")^
    let invalid = host.refresh(b,opened.id)^
    let repeat = host.refresh(b,opened.id)^
    let raw = model.get(local.status(b)^.state,opened.id)^.session.imports[0].raw
    let save_rejected = host.prepare_save(b,opened.id) ^ { ^ }
    let verified = local.verify(b)^
    io.delete(area ++ "/a/data.json")^
    io.mkdir(area ++ "/a/data.json")^
    let type_review = host.refresh(a,opened.id)^
    io.delete(area)^
    return [wrong is error,tampered is error,missing is error,
        c.compact(rolled_back.endpoint.replica) == c.compact(before.endpoint.replica),
        received == duplicate,exact == b'0001FEFF',not initial_cloud,p.status(acknowledged.endpoint)^.cloud,
        len(compacted.endpoint.journal) == 0,len(invalid.conflicts) == 1,
        len(repeat.conflicts) == 1,raw == "{ invalid",save_rejected is error,
        invalid.projection_error == null,lossy_json is error,verified.verified,
        type_review.workspace_review,not type_review.deleted]
}
