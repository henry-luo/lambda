// Test host supplies isolated paths and in-memory keys; production uses the OS vault.
import demo: ~~.mod_agent
import profile: ~~.mod_profile
import peer: lambda.sync.peer_agent
import cli: lambda.sync.cli
import identity: lambda.sync.peer_identity
import pairing: lambda.sync.pairing
import local: lambda.sync.local
import crypto: lambda.io.crypto
import codec: lambda.sync.codec
import sqlite: lambda.io.sqlite
import cloud: lambda.sync.cloud
import remote: lambda.sync.cloud_local
import cache: lambda.sync.cache
import maintenance: lambda.sync.cloud_maintenance
pn execute(request, key, environment) any^ {
    let paths = profile.paths(sys.proc.self.cwd#,request.profile,request.root)^
    let notify = pn (event)=> { output(event,request.events,{format:'json',atomic:true})^ }
    if (request.action == "create") { return profile.create(paths,request.user,key)^ }
    if (request.action == "legacy_fixture") {
        let space = pairing.space(request.user)^
        let credentials = {*:identity.create(space,crypto.key()^)^,version:1}
        let target = profile.directory(paths,request.user)^
        io.mkdir(paths.root)^; io.mkdir(target)^
        let box = crypto.seal(key,codec.encode(credentials)^,"lambda-peer-credentials-1")^
        open tx = (sqlite.create(target ++ "/device.sqlite")^) {
            sqlite.execute(tx,"CREATE TABLE credentials (id INTEGER PRIMARY KEY CHECK (id = 1), box BLOB NOT NULL)")^
            sqlite.execute(tx,"INSERT INTO credentials VALUES (1, ?)",[box])^
        }
        let repo = local.create(target ++ "/repo.sqlite",credentials.secret,space,paths.folder,"old")^
        local.scan(repo)^; local.snapshot(repo,"legacy snapshot")^
        output({version:1,user:request.user,folder:paths.folder},paths.root ++ "/profile.json",{format:'json',atomic:true})^
        return true
    }
    if (request.action == "migrate") { return profile.migrate(paths,request.user,key)^ }
    if (request.action == "cloud_backup" or request.action == "cache_cycle") {
        let saved = profile.load(paths)^
        let opened = peer.open_agent(profile.directory(profile.active(paths,saved),saved.user)^,key)^
        local.scan(opened.handle,null,true)^
        let config = remote.connect(profile.storage(saved)^,opened.credentials)^
        let backup = cloud.synchronize(opened.handle,opened.credentials,config)^
        if (request.action == "cloud_backup") { return backup.cloud_saved }
        let job = cache.plan(opened.handle,opened.credentials,config,64)^
        if (len(job.objects)^ < 2) raise error("test cache: expected two cold blobs")
        let removed = cache.step(opened.handle,config,job.id,128)^
        maintenance.prepare(opened.handle,opened.credentials,config)^
        var cut = maintenance.step(opened.handle,opened.credentials,config,128)^
        while (not cut.committed) { cut = maintenance.step(opened.handle,opened.credentials,config,128)^ }
        let restored = cache.hydrate(opened.handle,opened.credentials,config,128)^
        return [backup.cloud_saved,removed.complete,cut.committed,restored.restored >= 2,
            restored.remaining == 0,local.verify(opened.handle)^.verified]
    }
    if (request.action == "configure_cloud") {
        let saved = profile.load(paths)^
        let storage = request.storage
        output({*:saved,'s3-access-key':storage.access_key,'s3-secret-key':storage.secret_key,
            's3-bucket':storage.bucket,'s3-region':storage.region,'s3-endpoint':storage.endpoint},
            paths.root ++ "/profile.json",{format:'json',atomic:true})^
        return true
    }
    if (request.action == "host") { return demo.host(paths,request.user,key,notify,request.lifetime_ms or 120000)^ }
    if (request.action == "join") { return demo.join(paths,request.user,request.code,key,notify,request.lifetime_ms or 120000)^ }
    if (request.action == "run") { return demo.run(paths,key,notify,request.options or {})^ }
    if (request.action == "status") {
        let saved = profile.load(paths)^
        let result = peer.status(profile.directory(profile.active(paths,saved),saved.user)^,key)^
        output(result,request.events,{format:'json',atomic:true})^
        return true
    }
    raise error("test sync demo: unknown action")
}
pn main() { return cli.invoke(execute)^ }
