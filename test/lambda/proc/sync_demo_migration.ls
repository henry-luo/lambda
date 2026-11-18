import profile: ~~.~~.demo.sync.mod_profile
import identity: lambda.sync.peer_identity
import pairing: lambda.sync.pairing
import local: lambda.sync.local
import crypto: lambda.io.crypto
import sqlite: lambda.io.sqlite
import codec: lambda.sync.codec
import disk: lambda.io.disk
pn main() {
    let area = sys.proc.self.cwd ++ "/temp/sync-demo-migrate-" ++ string(sys.proc.self.pid)
    let paths = profile.paths(sys.proc.self.cwd,area ++ "/profile",area ++ "/tree")^
    let other = profile.paths(sys.proc.self.cwd,area ++ "/other",area ++ "/other-tree")^
    io.mkdir(paths.root)^; io.mkdir(paths.folder)^; io.mkdir(other.root)^; io.mkdir(other.folder)^
    let user = "migration-test"
    let key = crypto.key()^
    let oldspace = pairing.space(user)^
    let old = identity.create(oldspace,crypto.key()^)^
    let legacy = {*:old,version:1}
    let target = profile.directory(paths,user)^
    io.mkdir(target)^
    let box = crypto.seal(key,codec.encode(legacy)^,"lambda-peer-credentials-1")^
    open tx = (sqlite.create(target ++ "/device.sqlite")^) {
        sqlite.execute(tx,"CREATE TABLE credentials (id INTEGER PRIMARY KEY CHECK (id = 1), box BLOB NOT NULL)")^
        sqlite.execute(tx,"INSERT INTO credentials VALUES (1, ?)",[box])^
    }
    let oldrepo = local.create(target ++ "/repo.sqlite",old.secret,oldspace,paths.folder,"old")^
    output("preserve the current content",paths.folder ++ "/note")^
    local.scan(oldrepo)^
    local.snapshot(oldrepo,"legacy snapshot")^
    let saved = {version:1,user:user,folder:paths.folder,'s3-access-key':"test",'s3-secret-key':"secret",
        's3-bucket':"bucket",'s3-region':"test",'s3-endpoint':"http://127.0.0.1:1234"}
    output(saved,paths.root ++ "/profile.json",{format:'json',atomic:true})^
    let denied = profile.migrate(paths,user,crypto.key()^) ^ { ^ }
    let unchanged = codec.same_encoding(profile.load(paths)^,saved)^
    io.move(paths.folder,area ++ "/tree-held")^
    let interrupted = profile.migrate(paths,user,key) ^ { ^ }
    let still_old = codec.same_encoding(profile.load(paths)^,saved)^
    io.move(area ++ "/tree-held",paths.folder)^
    let migrated = profile.migrate(paths,user,key)^
    let active = profile.active(paths,migrated)
    let fresh = identity.load(profile.directory(active,user)^,key)^
    let current = local.open_repository(profile.directory(active,user)^ ++ "/repo.sqlite",fresh.secret,fresh.space)^
    let a = local.status(current)^
    let previous = local.status(oldrepo)^
    var oldbox = null
    open tx = (sqlite.existing(target ++ "/device.sqlite")^) { oldbox = sqlite.query(tx,"SELECT box FROM credentials WHERE id = 1")^[0][0] }
    let bkey = crypto.key()^
    let b = profile.prepare(other,user,bkey)^
    let invite = identity.invite(fresh,b.device)^
    let joined = profile.enroll(other,user,bkey,invite,fresh.device.id)^
    let later = profile.prepare(other,user,bkey)^
    let repeat = profile.migrate(paths,user,key) ^ { ^ }
    let checks = [denied is error,unchanged,interrupted is error,still_old,oldbox == box,exists(target ++ "/repo.sqlite"),
        migrated.agent != null,migrated.space != oldspace,fresh.version == 2,fresh.space == migrated.space,
        a.state.root == disk.resolve(paths.folder)^,len(a.state.disk)^ > 0,len(previous.state.disk)^ > 0,len(previous.state.snapshots)^ == 1,
        joined.space == migrated.space,later.space == migrated.space,repeat is error,
        migrated['s3-access-key'] == "test",input(paths.folder ++ "/note",'text')^ == "preserve the current content"]
    io.delete(area)^
    return checks
}
