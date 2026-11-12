import fixture: .mod_sync_cloud_store
import sqlite: lambda.io.sqlite
import service: lambda.sync.cloud_function
import schema: lambda.sync.cloud_schema
import catalog: lambda.sync.cloud_catalog
import pages: lambda.sync.metadata_pages
import receipts: lambda.sync.cloud_receipts
import codec: lambda.sync.codec
import identity: lambda.sync.peer_identity
import crypto: lambda.io.crypto
import s3: lambda.sync.s3

pn main() {
    let area = "temp/sync-paged-publication-" ++ string(sys.proc.self.pid#)
    io.mkdir(area)^
    let store = fixture.create(area ++ "/store.sqlite")^
    let owner = identity.create("paged-publication",crypto.key()^)^
    let control = schema.signed(owner.seed,"lambda-cloud-control-1",{version:1,space:owner.space,
        owner:owner.device,generation:1,storage:"catalog-2",members:[{device:owner.device,active:true}]})^
    service.bootstrap(store,control)^
    let prefix = schema.prefix(owner.space)^
    let keys = sort([for (i in 0 to 8192) catalog.data_key("objects",schema.digest(["object",i])^)^])^
    let bundle_key = catalog.data_key("bundles",schema.digest("bundle")^)^
    open tx = (sqlite.existing(area ++ "/store.sqlite")^) {
        for (key in [*keys,bundle_key]) { sqlite.execute(tx,"INSERT INTO objects VALUES (?, ?, ?)",[prefix ++ key,"abc",key])^ }
    }
    let root = pages.build(store,prefix,[for (key in [bundle_key,*keys]) {id:key,value:null}])^
    let current = catalog.load(store,prefix,control.body)^
    catalog.write(store,prefix,current,{*:current.value,objects_root:root})^
    var children = []
    for (offset in range(0,len(keys)^,64)) {
        children = [*children,receipts.stage(store,prefix,slice(keys,offset,offset+64)^,0)^.id]
    }
    var level = children
    while (len(level)^ > 1) {
        var parents = []
        for (offset in range(0,len(level)^,32)) {
            parents = [*parents,receipts.merge(store,prefix,slice(level,offset,offset+32)^,0)^.id]
        }
        level = parents
    }
    let bundle_receipt = receipts.stage(store,prefix,[bundle_key],0)^
    let combined = receipts.merge(store,prefix,[bundle_receipt.id,level[0]],0)^
    let publication = schema.signed(owner.seed,"lambda-cloud-publication-1",{version:2,space:owner.space,
        actor:owner.device.id,sequence:1,previous:null,clock:[],
        bundle:{key:bundle_key,bytes:3,checksum:s3.checksum("abc")^},inventory:schema.digest(root)^,
        dependencies:combined.id,receipts:[combined.id]})^
    let request = schema.request(owner,"commit",{publication:publication,expected:null},1000000,schema.digest("nonce")^)^
    let committed = service.handle(store,request,1000000)^
    let replay = service.handle(store,request,1000000)^
    let stale = receipts.merge(store,prefix,[combined.id],1) ^ { ^ }
    let observed_catalog = catalog.load(store,prefix,control.body)^
    let header = store.get(prefix ++ "catalog")^
    let dependencies = pages.all(store,prefix,combined.root)^
    io.delete(area)^
    return [root.count == 8194,combined.root.count == 8194,len(dependencies)^ == 8194,
        committed.published,replay.published,stale is error,observed_catalog.value.objects == [],
        observed_catalog.value.objects_root == root,len(header.body)^ < 8192]
}
