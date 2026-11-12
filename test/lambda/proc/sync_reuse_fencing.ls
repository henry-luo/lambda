import fixture: .mod_sync_cloud_store
import service: lambda.sync.cloud_function
import schema: lambda.sync.cloud_schema
import catalog: lambda.sync.cloud_catalog
import codec: lambda.sync.codec
import identity: lambda.sync.peer_identity
import crypto: lambda.io.crypto
import s3: lambda.sync.s3

pn call(store, credentials, kind, body) any^ {
    return service.handle(store,schema.request(credentials,kind,body,1000000,schema.digest([kind,body])^)^,1000000)^
}
pn upload(store, credentials, key) any^ {
    call(store,credentials,"upload",{objects:[{key:key,bytes:3,checksum:s3.checksum("abc")^}]})^
    store.write(schema.prefix(credentials.space)^ ++ key,"abc",null)^
    return true
}
fn publication(credentials, key, receipt) any^ => schema.signed(credentials.seed,"lambda-cloud-publication-1",
    {version:2,space:credentials.space,actor:credentials.device.id,sequence:1,previous:null,clock:[],
     bundle:{key:key,bytes:3,checksum:s3.checksum("abc")^},inventory:schema.digest([])^,
     dependencies:receipt.id,receipts:[receipt.id]})^
fn plan(remote, fingerprint) any^ => {version:1,epoch:remote.next_epoch,
    heads:schema.catalog_heads({heads:remote.heads})^,fingerprint:schema.digest(fingerprint)^}
pn cut(store, credentials, remote, retained, label) any^ {
    let selected = plan(remote,label)^
    let begun = call(store,credentials,"maint_begin",{expected:remote.catalog_etag,plan:selected})^
    let key = catalog.data_key("bundles",schema.digest(label)^,selected.epoch)^
    upload(store,credentials,key)^
    let staged = call(store,credentials,"stage",{keys:[retained,key]})^
    let checkpoint = schema.signed(credentials.seed,"lambda-cloud-maintenance-1",
        {version:1,epoch:selected.epoch,covered:selected.heads,publication:publication(credentials,key,staged)^})^
    call(store,credentials,"maint_commit",{id:begun.id,checkpoint:checkpoint})^
    var pruned = call(store,credentials,"maint_prune",{id:begun.id,budget:1})^
    while (not pruned.complete) { pruned = call(store,credentials,"maint_prune",{id:begun.id,budget:64})^ }
    return {id:begun.id,key:key,epoch:selected.epoch}
}
pn main() {
    let area = "temp/sync-reuse-fencing-" ++ string(sys.proc.self.pid#)
    io.mkdir(area)^
    let store = fixture.create(area ++ "/store.sqlite")^
    let owner = identity.create("reuse-fencing",crypto.key()^)^
    let control = schema.signed(owner.seed,"lambda-cloud-control-1",{version:1,space:owner.space,
        owner:owner.device,generation:1,storage:"catalog-2",members:[{device:owner.device,active:true}]})^
    service.bootstrap(store,control)^
    let prefix = schema.prefix(owner.space)^
    let retained = catalog.data_key("objects",schema.digest("retained")^)^
    let original_bundle = catalog.data_key("bundles",schema.digest("original")^)^
    upload(store,owner,retained)^
    upload(store,owner,original_bundle)^
    let initial_receipt = call(store,owner,"stage",{keys:[retained,original_bundle]})^
    let first = publication(owner,original_bundle,initial_receipt)^
    call(store,owner,"commit",{publication:first,expected:null})^
    let pin = call(store,owner,"cache_pin",{id:schema.digest("pin")^,
        objects:[schema.digest("retained")^],keys:[retained]})^
    let before = call(store,owner,"pull",{})^
    let abandoned_plan = plan(before,"abandon")^
    let abandoned = call(store,owner,"maint_begin",{expected:before.catalog_etag,plan:abandoned_plan})^
    let abandoned_key = catalog.data_key("objects",schema.digest("abandoned")^,1)^
    upload(store,owner,abandoned_key)^
    call(store,owner,"maint_abort",{id:abandoned.id})^
    let after_abort = call(store,owner,"pull",{})^
    let rejected = call(store,owner,"stage",{keys:[abandoned_key]}) ^ { ^ }
    let resumed = call(store,owner,"maint_begin",{expected:after_abort.catalog_etag,plan:abandoned_plan}) ^ { ^ }
    let second = cut(store,owner,after_abort,retained,"second")^
    let retained_after_second = store.get(prefix ++ retained)^
    let abandoned_removed = store.get(prefix ++ abandoned_key)^ == null
    let third = cut(store,owner,call(store,owner,"pull",{})^,retained,"third")^
    store.write(prefix ++ abandoned_key,"abc",null)^
    let reaped = call(store,owner,"maint_reap",{id:second.id,budget:64})^
    let current = catalog.load(store,prefix,control.body)^
    let old_retry = call(store,owner,"commit",{publication:first,expected:null})^
    let stale_publication = schema.signed(owner.seed,"lambda-cloud-publication-1",
        {*:first.body,sequence:2,previous:schema.digest(first)^})^
    let old_receipt = call(store,owner,"commit",{publication:stale_publication,expected:schema.digest(first)^}) ^ { ^ }
    let report = [pin.reserved,after_abort.epoch == 0,after_abort.next_epoch == 2,rejected is error,
        resumed is error,second.epoch == 2,retained_after_second.body == "abc",abandoned_removed,
        third.epoch == 3,reaped.complete,store.get(prefix ++ retained)^.body == "abc",
        store.get(prefix ++ third.key)^.body == "abc",store.get(prefix ++ second.key)^ == null,
        store.get(prefix ++ abandoned_key)^ == null,current.value.retirements_root.count == 2,
        old_retry.published,old_receipt is error]
    io.delete(area)^
    return report
}
