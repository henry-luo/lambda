import fixture: .mod_sync_cloud_store
import service: lambda.sync.cloud_function
import schema: lambda.sync.cloud_schema
import catalog: lambda.sync.cloud_catalog
import codec: lambda.sync.codec
import pages: lambda.sync.metadata_pages
import identity: lambda.sync.peer_identity
import crypto: lambda.io.crypto
import s3: lambda.sync.s3
pn call(store, credentials, kind, body) any^ {
    return service.handle(store,schema.request(credentials,kind,body,1000000,schema.digest([kind,body])^)^,1000000)^
}
pn main() {
    let area = "temp/sync-cloud-audit-" ++ string(sys.proc.self.pid#)
    io.mkdir(area)^
    let store = fixture.create(area ++ "/store.sqlite")^
    let owner = identity.create("audit-test",crypto.key()^)^
    let other = identity.create(owner.space,owner.secret)^
    let control = schema.signed(owner.seed,"lambda-cloud-control-1",{version:1,space:owner.space,
        owner:owner.device,generation:1,storage:"catalog-2",members:[{device:owner.device,active:true},{device:other.device,active:true}]})^
    service.bootstrap(store,control)^
    let prefix = schema.prefix(owner.space)^
    let initial = call(store,owner,"pull",{})^
    let denied = call(store,other,"audit_begin",{expected:initial.catalog_etag}) ^ { ^ }
    let claim = catalog.claim_epoch(store,prefix,catalog.load(store,prefix,control.body)^.value,1,schema.digest("crash before activation")^ )^
    let begun = call(store,owner,"audit_begin",{expected:initial.catalog_etag})^
    let unknown = catalog.data_key("objects",schema.digest("orphan")^,9)^
    store.write(prefix ++ unknown,"ciphertext",null)^
    let upload = call(store,owner,"upload",{objects:[{key:catalog.data_key("objects",schema.digest("blocked")^,2)^,
        bytes:3,checksum:s3.checksum("abc")^}]}) ^ { ^ }
    var scanned = call(store,owner,"audit_step",{id:begun.audit.id,budget:1})^
    while (not scanned.audit.complete) { scanned = call(store,owner,"audit_step",{id:begun.audit.id,budget:1})^ }
    let remote = call(store,owner,"pull",{})^
    let inventory = pages.all(store,prefix,scanned.audit.inventory)^
    let forged = schema.signed(owner.seed,"lambda-cloud-audit-1",{version:1,audit:begun.audit.id,
        inventory:scanned.audit.inventory,checked:0,bad:0,heads:scanned.audit.heads,base:scanned.audit.base})^
    let plan = {version:1,epoch:remote.next_epoch,heads:[],fingerprint:schema.digest("invalid report")^}
    let incomplete = call(store,owner,"maint_begin",{expected:remote.catalog_etag,plan:plan,
        audit:begun.audit.id,attestation:forged}) ^ { ^ }
    let conditional = call(store,owner,"read",{keys:[unknown],etags:["observed"]})^
    call(store,owner,"audit_abort",{id:begun.audit.id})^
    let after = call(store,owner,"pull",{})^
    let discovered = call(store,owner,"backup_discover",{limit:1})^
    store.erase(prefix ++ "catalog")^
    let missing = call(store,owner,"pull",{}) ^ { ^ }
    let found_without_catalog = call(store,owner,"backup_discover",{limit:1})^
    let recovered = call(store,owner,"recover_catalog",{snapshot:initial.catalog_backup})^
    let locked = call(store,owner,"pull",{})^
    // even loss of the mutable high-water record cannot recycle an immutable allocation.
    store.erase(prefix ++ "generation")^
    let collision = catalog.claim_epoch(store,prefix,catalog.load(store,prefix,control.body)^.value,1,schema.digest("recovery")^)^
    let high = catalog.available_epoch(store,prefix,catalog.load(store,prefix,control.body)^.value)^
    let report = [denied is error,claim,begun.started,upload is error,scanned.audit.complete,
        remote.next_epoch == 10,len(inventory)^ == 1,inventory[0].id == unknown,
        scanned.audit.quarantine.count == 1,incomplete is error,
        conditional[0].capability.headers['if-match'] == "observed",after.phase == 'open',
        not after.maintenance_allowed,len(discovered.entries)^ == 1,missing is error,
        len(found_without_catalog.entries)^ == 1,recovered.recovered,not locked.maintenance_allowed,
        not collision,high == 2]
    io.delete(area)^
    return report
}
