import rv: lambda.sync.rendezvous
import schema: lambda.sync.cloud_schema
import service: lambda.sync.cloud_function
import identity: lambda.sync.peer_identity
import crypto: lambda.io.crypto
import fixture: .mod_sync_cloud_store
import codec: lambda.sync.codec
pn call(store, credentials, kind, body, at=1000000) any^ {
    return service.handle(store,schema.request(credentials,kind,body,at,schema.digest([kind,body,at])^)^,at)^
}
pn main() {
    let area = "temp/sync-rendezvous-" ++ string(sys.proc.self.pid#)
    io.mkdir(area)^
    let store = fixture.create(area ++ "/store.sqlite")^
    let a0 = identity.create("rendezvous-test",crypto.key()^)^
    let b0 = identity.create(a0.space,a0.secret)^
    let a = identity.trust(a0,b0.device)^
    let b = identity.trust(b0,a.device)^
    let control = schema.signed(a.seed,"lambda-cloud-control-1",{version:1,space:a.space,owner:a.device,
        generation:1,members:[{device:a.device,active:true},{device:b.device,active:true}]})^
    service.bootstrap(store,control)^
    let session = schema.digest("one session")^
    let first = rv.create(a,"offer",b.device.id,1,session,{sdp:"offer"},1000000)^
    let written = call(store,a,"rendezvous_put",{record:first,expected:null})^
    let received = call(store,b,"rendezvous_get",{kind:"offer",actors:[a.device.id]})^
    let opened = rv.decode(b,received[0],"offer",b.device.id,1000000,session)^
    let duplicate = call(store,a,"rendezvous_put",{record:first,expected:null})^
    let next = rv.create(a,"offer",b.device.id,2,session,{sdp:"next"},1000001)^
    let stale = call(store,a,"rendezvous_put",{record:next,expected:null},1000001)^
    let updated = call(store,a,"rendezvous_put",{record:next,expected:written.etag},1000001)^
    let replay = call(store,a,"rendezvous_put",{record:first,expected:updated.etag},1000001)^
    let forged = rv.decode(b,{*:next,body:{*:next.body,target:a.device.id}},"offer",b.device.id,1000001) ^ { ^ }
    let wrong_session = rv.decode(b,next,"offer",b.device.id,1000001,schema.digest("other")^) ^ { ^ }
    let expired = call(store,b,"rendezvous_get",{kind:"offer",actors:[a.device.id]},1060002)^
    let preserved = call(store,a,"rendezvous_current",{kind:"offer",target:b.device.id},1060002)^
    let wrong_author = call(store,b,"rendezvous_put",{record:next,expected:updated.etag},1000001) ^ { ^ }
    let presence = rv.create(a,"presence",null,1,session,{rtc:true},1000000)^
    let announced = call(store,a,"rendezvous_put",{record:presence,expected:null})^
    let visible = call(store,b,"rendezvous_get",{kind:"presence",actors:[a.device.id]})^
    let revoked_control = schema.signed(a.seed,"lambda-cloud-control-1",{*:control.body,generation:2,
        members:[{device:a.device,active:true},{device:b.device,active:false}]})^
    let captured = call(store,a,"pull",{})^
    call(store,a,"membership",{control:revoked_control,expected:captured.control_etag})^
    let denied = call(store,b,"rendezvous_get",{kind:"presence",actors:[a.device.id]}) ^ { ^ }
    let full = rv.create(a,"presence",null,2,session,binary([for (i in 0 to 29799) i%256])^,1000001)^
    let bounded = call(store,a,"rendezvous_put",{record:full,expected:announced.etag},1000001)^
    let response_bytes = len(binary(codec.encode({ok:true,body:[for (i in 0 to 7) full]})^)^)^
    let huge = schema.signed(a.seed,rv.domain,{*:full.body,sequence:3,box:binary([for (i in 0 to 65535) 0])^})^
    let oversized = call(store,a,"rendezvous_put",{record:huge,expected:bounded.etag},1000001) ^ { ^ }
    let over_page = call(store,a,"rendezvous_get",{kind:"presence",actors:[for (i in 0 to 8) schema.digest(i)^]},1000001) ^ { ^ }
    let result = [written.written,len(received)^ == 1,opened.payload.sdp == "offer",duplicate.written,
        not stale.written,updated.written,not replay.written,forged is error,wrong_session is error,
        len(expired)^ == 0,preserved.record.body.sequence == 2,wrong_author is error,announced.written,
        rv.decode(b,visible[0],"presence",null,1000000)^.payload.rtc,
        rv.lan_name(a,b.device.id)^ == rv.lan_name(b,b.device.id)^,denied is error,
        bounded.written,response_bytes <= 1048576,
        oversized is error and contains(oversized.message,"record budget")^,
        over_page is error and contains(over_page.message,"query budget")^]
    io.delete(area)^
    return result
}
