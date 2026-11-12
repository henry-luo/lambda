import identity: lambda.sync.peer_identity
import crypto: lambda.io.crypto
import local: lambda.sync.local
import bundle: lambda.sync.cloud_bundle
import cloud: lambda.sync.cloud
import schema: lambda.sync.cloud_schema
import codec: lambda.sync.codec
import protocol: lambda.sync.protocol
import transport: lambda.sync.cloud_transport
pn main() {
    let area = "temp/sync-cloud-repository-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/a")^
    io.mkdir(area ++ "/b")^
    let a = identity.create("cloud-repository",crypto.key()^)^
    let b = identity.create(a.space,a.secret)^
    let source = local.create(area ++ "/a.sqlite",a.secret,a.space,area ++ "/a","source")^
    let target = local.create(area ++ "/b.sqlite",b.secret,b.space,area ++ "/b","target")^
    output("saved content",area ++ "/a/note")^
    local.scan(source)^
    local.snapshot(source,"first")^
    let captured = bundle.capture(source,a)^
    let descriptor = transport.descriptor(captured.key,captured.bytes)^
    let publication = {bundle:descriptor,actor:a.device.id,clock:captured.payload.saved.frontier,
        inventory:schema.digest(captured.payload.ids)^,
        dependencies:schema.dependency_digest(bundle.dependencies(captured.payload.ids,captured.key)^)^}
    let payload = bundle.decode(b,publication,captured.bytes)^
    let wrong_key = bundle.decode({*:b,secret:crypto.key()^},publication,captured.bytes) ^ { ^ }
    let tampered = bundle.decode(b,publication,captured.bytes ++ b'00') ^ { ^ }
    let unavailable = bundle.install_checkpoint(target,b,a.device,payload) ^ { ^ }
    local.import_objects(target,local.export_objects(source,captured.payload.ids)^)^
    bundle.install_checkpoint(target,b,a.device,payload)^
    let installed = local.materialize(target)^
    let before = protocol.status(local.status(target)^.state.endpoint)^.cloud
    let text = input(area ++ "/b/note",'text')^
    let provider = schema.digest("provider")^
    let record = schema.digest("publication")^
    local.record_cloud(target,provider,a.device.id,1,record,payload.saved,payload.ids)^
    let complete = local.status(target)^.state
    let after = cloud.covered(complete,provider)^
    let other = cloud.covered(complete,schema.digest("another-provider")^)^
    let fork = local.record_cloud(target,provider,a.device.id,1,schema.digest("fork")^,payload.saved,payload.ids) ^ { ^ }
    local.record_cloud_control(target,provider,3,schema.digest("control3")^)^
    let rollback_result = local.record_cloud_control(target,provider,2,schema.digest("control2")^) ^ { ^ }
    output("unsaved to cloud",area ++ "/b/note")^
    local.scan(target)^
    local.record_cloud(target,provider,a.device.id,1,record,payload.saved,payload.ids)^
    let pending = cloud.covered(local.status(target)^.state,provider)^
    let verified = local.verify(target)^
    io.delete(area)^
    return [wrong_key is error,tampered is error,unavailable is error,not before,text == "saved content",
        len(complete.snapshots) == 1,after,not other,fork is error,rollback_result is error,not pending,verified.verified]
}
