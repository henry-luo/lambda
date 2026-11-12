import fixture: .mod_sync_cloud_store
import service: lambda.sync.cloud_function
import schema: lambda.sync.cloud_schema
import identity: lambda.sync.peer_identity
import crypto: lambda.io.crypto
import codec: lambda.sync.codec
import catalog: lambda.sync.cloud_catalog
import maintenance: lambda.sync.cloud_maintenance_function
import s3: lambda.sync.s3

pn call(store, credentials, kind, body) any^ { return service.handle(store,
    schema.request(credentials,kind,body,1000000,schema.digest([kind,body])^)^,1000000)^ }
fn publication(credentials, key, receipt, sequence=1, previous=null) any^ =>
    schema.signed(credentials.seed,"lambda-cloud-publication-1",{version:1,space:credentials.space,
        actor:credentials.device.id,sequence:sequence,previous:previous,clock:[],
        bundle:{key:key,bytes:3,checksum:s3.checksum("abc")^},inventory:schema.digest([])^,
        dependencies:schema.dependency_digest(receipt.keys)^,receipts:[receipt.id]})^
pn main() {
    let area = "temp/sync-retention-cloud-" ++ string(sys.proc.self.pid#)
    io.mkdir(area)^
    let store = fixture.create(area ++ "/store.sqlite")^
    let a = identity.create("retention-cloud",crypto.key()^)^
    let b = identity.create(a.space,a.secret)^
    let control = schema.signed(a.seed,"lambda-cloud-control-1",{version:1,space:a.space,owner:a.device,generation:1,
        members:[{device:a.device,active:true},{device:b.device,active:true}]})^
    service.bootstrap(store,control)^
    let prefix = schema.prefix(a.space)^
    let object_hex = schema.digest("same logical bytes")^
    let old_key = catalog.data_key("objects",object_hex,0)^
    let old_bundle = catalog.data_key("bundles",schema.digest("old bundle")^,0)^
    store.write(prefix ++ old_key,"abc",null)^
    store.write(prefix ++ old_bundle,"abc",null)^
    let staged = call(store,a,"stage",{keys:[old_key,old_bundle]})^
    let first = publication(a,old_bundle,{id:staged.id,keys:[old_key,old_bundle]})^
    let committed = call(store,a,"commit",{publication:first,expected:null})^
    let initial = call(store,a,"pull",{})^
    let plan = {version:1,epoch:1,heads:maintenance.heads({heads:initial.heads})^,fingerprint:schema.digest("captured")^}
    let pin_id = schema.digest("cache pin")^
    let pin = call(store,b,"cache_pin",{id:pin_id,objects:[object_hex]})^
    let stale = call(store,a,"maint_begin",{expected:initial.catalog_etag,plan:plan}) ^ { ^ }
    let captured = call(store,a,"pull",{})^
    let reserved_key = catalog.data_key("objects",schema.digest("pending upload")^,0)^
    call(store,b,"upload",{objects:[{key:reserved_key,bytes:3,checksum:s3.checksum("abc")^}]})^
    let reserved_catalog = call(store,a,"pull",{})^
    let protected_upload = call(store,a,"maint_begin",{expected:reserved_catalog.catalog_etag,plan:plan}) ^ { ^ }
    call(store,b,"cancel_publication",{})^
    let clean = call(store,a,"pull",{})^
    let denied = call(store,b,"maint_begin",{expected:clean.catalog_etag,plan:plan}) ^ { ^ }
    let begun = call(store,a,"maint_begin",{expected:clean.catalog_etag,plan:plan})^
    let next = publication(a,old_bundle,{id:staged.id,keys:[old_key,old_bundle]},2,schema.digest(first)^)^
    let paused = call(store,a,"commit",{publication:next,expected:committed.etag}) ^ { ^ }
    let old_upload = call(store,a,"upload",{objects:[{key:old_key,bytes:3,checksum:s3.checksum("abc")^}]}) ^ { ^ }
    let new_key = catalog.data_key("objects",object_hex,1)^
    let new_bundle = catalog.data_key("bundles",schema.digest("new bundle")^,1)^
    store.write(prefix ++ new_bundle,"abc",null)^
    let bundle_only = call(store,a,"stage",{keys:[new_bundle]})^
    let incomplete_pub = publication(a,new_bundle,{id:bundle_only.id,keys:[new_bundle]})^
    let incomplete = schema.signed(a.seed,"lambda-cloud-maintenance-1",{version:1,epoch:1,covered:plan.heads,publication:incomplete_pub})^
    let missing_pin = call(store,a,"maint_commit",{id:begun.id,checkpoint:incomplete}) ^ { ^ }
    store.write(prefix ++ new_key,"abc",null)^
    let copied = call(store,a,"stage",{keys:[new_key,new_bundle]})^
    let compact = publication(a,new_bundle,{id:copied.id,keys:[new_key,new_bundle]})^
    let checkpoint = schema.signed(a.seed,"lambda-cloud-maintenance-1",{version:1,epoch:1,covered:plan.heads,publication:compact})^
    let cut = call(store,a,"maint_commit",{id:begun.id,checkpoint:checkpoint})^
    let retry = call(store,a,"maint_commit",{id:begun.id,checkpoint:checkpoint})^
    let old_receipt = call(store,a,"stage",{keys:[old_key,old_bundle]}) ^ { ^ }
    let retired_publish = call(store,a,"commit",{publication:next,expected:committed.etag}) ^ { ^ }
    let one = call(store,b,"maint_prune",{id:begun.id,budget:1})^
    let resumed = call(fixture.adapter(area ++ "/store.sqlite"),b,"maint_prune",{id:begun.id,budget:64})^
    // a delayed upload may recreate retired garbage; a bounded reaper can repeat old-key deletion.
    store.write(prefix ++ old_key,"abc",null)^
    let reap = call(store,b,"maint_reap",{id:begun.id,budget:64})^
    let reaped = store.get(prefix ++ old_key)^ == null
    // a delayed delete can only name the old immutable physical generation.
    store.erase(prefix ++ old_key)^
    let restored = store.get(prefix ++ new_key)^
    let remote = call(store,b,"pull",{})^
    // missing authority must fail closed; recovery uses an explicit validated immutable backup.
    store.erase(prefix ++ "catalog")^
    let missing_catalog = call(store,a,"pull",{}) ^ { ^ }
    let recovered = call(store,a,"recover_catalog",{snapshot:remote.catalog_backup})^
    let after_recovery = call(store,a,"pull",{})^
    let recovery_plan = {*:plan,epoch:2,heads:maintenance.heads({heads:after_recovery.heads})^}
    let audit_required = call(store,a,"maint_begin",{expected:after_recovery.catalog_etag,plan:recovery_plan}) ^ { ^ }
    io.delete(area)^
    return [pin.reserved,stale is error,denied is error,begun.started,paused is error,
        old_upload is error,missing_pin is error,cut.committed,retry.committed,
        old_receipt is error,retired_publish is error,one.next == 1,resumed.complete,
        restored.body == "abc",remote.epoch == 1,remote.checkpoint == checkpoint,
        schema.data_key(new_key)^,not schema.data_key("objects/01/" ++ object_hex)^,protected_upload is error,reap.complete,reaped,
        missing_catalog is error,recovered.recovered,after_recovery.epoch == 1,
        after_recovery.checkpoint == checkpoint,audit_required is error,not after_recovery.maintenance_allowed]
}
