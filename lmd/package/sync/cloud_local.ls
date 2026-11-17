// Trusted local host invokes the existing authenticated cloud service against its own S3 account.
import s3: .s3
import service: .cloud_function
import schema: .cloud_schema
import codec: .codec
import crypto: lambda.io.crypto
import hex: lambda.security.tuf.metadata

pub pn connect(storage, credentials) any^ {
    s3.validate(storage)^
    let store = s3.adapter(storage)
    let path = schema.prefix(credentials.space)^ ++ "control"
    var record = store.get(path)^
    if (record == null) {
        let signed = schema.signed(credentials.seed,"lambda-cloud-control-1",{version:1,space:credentials.space,
            owner:credentials.device,generation:1,storage:"catalog-2",
            members:[for (device in [credentials.device,*credentials.members]) {device:device,active:true}]})^
        // Another trusted device may win initialization; validate the resulting control either way.
        service.bootstrap(store,signed) ^ { null }
        record = store.get(path)^
        if (record == null) raise error("sync cloud: storage initialization failed")
    }
    let envelope = codec.decode(record.body,131072)^
    let control = schema.control(envelope,envelope.body.owner.id)^
    if (control.space != credentials.space or not contains(
        [credentials.device.id,*[for (member in credentials.members) member.id]],control.owner.id)^)
        raise error("sync cloud: unknown space owner")
    schema.member(control,credentials.device.id)^
    if (control.owner.id == credentials.device.id) {
        // A pairing explicitly authorizes a new device; existing cloud revocations stay inactive.
        let added = [for (device in credentials.members where
            not any([for (entry in control.members) entry.device.id == device.id])) {device:device,active:true}]
        if (len(added)^ > 0) {
            let next = schema.signed(credentials.seed,"lambda-cloud-control-1",{*:control,
                generation:control.generation+1,members:[*control.members,*added]})^
            let moment = now().unix
            let request = schema.request(credentials,"membership",{control:next,expected:record.etag},
                moment,hex.hex(crypto.key()^)^)^
            let updated = service.handle(store,request,moment)^
            if (updated.written != true) raise error("sync cloud: membership changed; retry")
        }
    }
    return {url:storage.endpoint ++ "/" ++ storage.bucket,authority:control.owner.id,storage:storage}
}
