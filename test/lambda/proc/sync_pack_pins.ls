import fixture: .mod_sync_cloud_store
import service: lambda.sync.cloud_function
import schema: lambda.sync.cloud_schema
import identity: lambda.sync.peer_identity
import cache: lambda.sync.cache
import maintenance: lambda.sync.cloud_maintenance
import hex: lambda.security.tuf.metadata
import crypto: lambda.io.crypto
pn call(store, credentials, kind, body) any^ {
    return service.handle(store,schema.request(credentials,kind,body,1000000,schema.digest([kind,body])^)^,1000000)^
}
pn main() {
    let area = "temp/sync-pack-pins-" ++ string(sys.proc.self.pid)
    io.mkdir(area)^
    let store = fixture.create(area ++ "/store")^
    let owner = identity.create("pack-pins",crypto.key()^)^
    let control = schema.signed(owner.seed,"lambda-cloud-control-1",{version:1,space:owner.space,
        owner:owner.device,generation:1,storage:"catalog-2",members:[{device:owner.device,active:true}]})^
    service.bootstrap(store,control)^
    let prefix = schema.prefix(owner.space)^
    let a = schema.digest("first blob")^
    let b = schema.digest("second blob")^
    let key = "objects/" ++ schema.digest("shared pack")^
    let bytes = "test ciphertext"
    call(store,owner,"upload",{objects:[{key:key,bytes:len(bytes)^,checksum:"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="}]})^
    store.write(prefix ++ key,bytes,null)^
    let id = schema.digest("pin")^
    let reserved = call(store,owner,"cache_pin",{id:id,objects:[b,a],keys:[key,key]})^
    let remote = call(store,owner,"pull",{})^
    let pin = remote.pins[0]
    let bad = call(store,owner,"cache_pin",{id:schema.digest("bad")^,objects:[a],
        keys:["objects/" ++ schema.digest("unregistered")^]}) ^ { ^ }
    let released = call(store,owner,"cache_release",{id:id})^
    let after = call(store,owner,"pull",{})^
    let checks = [reserved.reserved,pin.objects == sort([a,b])^,pin.keys == [key,key],
        cache.physical_key(pin,hex.unhex(b)^,0)^ == key,
        maintenance.pin_key(remote,hex.unhex(a)^)^ == key,
        bad is error,released.released,len(after.pins)^ == 0]
    io.delete(area)^
    return checks
}
