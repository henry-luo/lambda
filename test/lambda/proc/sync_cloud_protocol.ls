import fixture: .mod_sync_cloud_store
import service: lambda.sync.cloud_function
import schema: lambda.sync.cloud_schema
import identity: lambda.sync.peer_identity
import crypto: lambda.io.crypto
import codec: lambda.sync.codec
import s3: lambda.sync.s3
let moment = 1000000
pn call(store, credentials, kind, body, issued=moment) any^ {
    return service.handle(store,schema.request(credentials,kind,body,issued,
        "0000000000000000000000000000000000000000000000000000000000000001")^,moment)^
}
pn main() {
    let area = "temp/sync-cloud-protocol-" ++ string(sys.proc.self.pid#)
    io.mkdir(area)^
    let store = fixture.create(area ++ "/store.sqlite")^
    let a = identity.create("cloud-protocol",crypto.key()^)^
    let b = identity.create(a.space,a.secret)^
    let stranger = identity.create(a.space,a.secret)^
    let signed = schema.signed(a.seed,"lambda-cloud-control-1",{version:1,space:a.space,owner:a.device,generation:1,
        members:[{device:a.device,active:true},{device:b.device,active:true}]})^
    service.bootstrap(store,signed)^
    let duplicate = service.bootstrap(store,signed) ^ { ^ }
    let expired = call(store,a,"pull",{},0) ^ { ^ }
    let unknown = call(store,stranger,"pull",{}) ^ { ^ }
    let scope = call(store,a,"read",{keys:["heads/" ++ a.device.id]}) ^ { ^ }
    let object_key = "objects/" ++ schema.digest("object")^
    let bundle_key = "bundles/" ++ schema.digest("bundle")^
    let missing = call(store,a,"stage",{keys:[object_key]}) ^ { ^ }
    let prefix = schema.prefix(a.space)^
    store.write(prefix ++ object_key,"object",null)^
    store.write(prefix ++ bundle_key,"bundle",null)^
    let receipt = call(store,a,"stage",{keys:[object_key,bundle_key]})^
    let repeated = call(store,b,"stage",{keys:[bundle_key,object_key]})^
    let descriptor = {key:bundle_key,bytes:6,checksum:s3.checksum("bundle")^}
    let publication = schema.signed(a.seed,"lambda-cloud-publication-1",{version:1,space:a.space,actor:a.device.id,
        sequence:1,previous:null,clock:[],bundle:descriptor,inventory:schema.digest([])^,
        dependencies:schema.dependency_digest([object_key,bundle_key])^,receipts:[receipt.id]})^
    let committed = call(store,a,"commit",{publication:publication,expected:null})^
    let retry = call(store,a,"commit",{publication:publication,expected:null})^
    let impersonation = call(store,b,"commit",{publication:publication,expected:null}) ^ { ^ }
    let body = {*:publication.body,sequence:2,previous:schema.digest(publication)^}
    let next = schema.signed(a.seed,"lambda-cloud-publication-1",body)^
    let stale = call(store,a,"commit",{publication:next,expected:"stale"})^
    let incomplete = schema.signed(a.seed,"lambda-cloud-publication-1",{*:body,receipts:[schema.digest("missing")^]})^
    let rejected = call(store,a,"commit",{publication:incomplete,expected:committed.etag}) ^ { ^ }
    let forged = call(store,a,"commit",{publication:{*:next,signature:b'00'},expected:committed.etag}) ^ { ^ }
    let read = call(fixture.adapter(area ++ "/store.sqlite"),b,"pull",{})^
    let revoked = schema.signed(a.seed,"lambda-cloud-control-1",{*:signed.body,generation:2,
        members:[{device:a.device,active:true},{device:b.device,active:false}]})^
    let changed = call(store,a,"membership",{control:revoked,expected:read.control_etag})^
    let denied = call(store,b,"pull",{}) ^ { ^ }
    let capabilities = call(store,a,"upload",{objects:[{key:"objects/" ++ schema.digest("new")^,
        bytes:3,checksum:s3.checksum("abc")^}]})^
    io.delete(area)^
    return [duplicate is error,expired is error,unknown is error,scope is error,missing is error,
        receipt.id == repeated.id,committed.published,retry.etag == committed.etag,
        impersonation is error,not stale.published,rejected is error,forged is error,
        len(read.heads) == 1 and read.heads[0].publication == publication,changed.written,denied is error,
        capabilities[0].capability.headers.'if-none-match' == "*" and
            capabilities[0].capability.headers.'content-length' == "3"]
}
