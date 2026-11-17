// Public profile metadata stays outside the tree; the host supplies its vault-backed wrapping key.
import pairing: lambda.sync.pairing
import identity: lambda.sync.peer_identity
import agent: lambda.sync.peer_agent
import local: lambda.sync.local
import crypto: lambda.io.crypto
import v: lambda.sync.schema
import disk: lambda.io.disk
import hex: lambda.security.tuf.metadata
import codec: lambda.sync.codec

pub fn paths(home, root=null, folder=null) any^ {
    if (not (home is string) or not starts_with(home,"/")) raise error("Sync needs an absolute home directory.")
    let selected = {root:root or (home ++ "/Library/Application Support/Lambda Sync Demo"),
        folder:folder or (home ++ "/lambda-sync")}
    if (not starts_with(selected.root,"/") or not starts_with(selected.folder,"/") or
        selected.root == selected.folder or starts_with(selected.root,selected.folder ++ "/") or
        starts_with(selected.folder,selected.root ++ "/")) raise error("Keep the sync profile outside the synced folder.")
    return selected
}
pub fn directory(paths, value) string^ => paths.root ++ "/" ++ pairing.account(value)^ ++
    if (paths.agent == null) "" else "-v2-" ++ paths.agent
fn valid_token(value) bool^ => value is string and len(value) == 64 and value == lower(value) and hex.hex_valid(value)
fn valid_space(user, space) any^ {
    let base = pairing.space(user)^
    if (space == base) { return true }
    let prefix = base ++ "-v2-"
    return space is string and starts_with(space,prefix)^ and valid_token(slice(space,len(prefix)^)^)^
}
pub fn active(paths, saved) => if (saved == null) paths else {*:paths,agent:saved.agent,space:saved.space}
pub pn load(paths) any^ {
    if (not exists(paths.root ++ "/profile.json")) { return null }
    let saved = input(paths.root ++ "/profile.json",'json')^
    v.record(saved,["version","user","folder"],["s3-access-key","s3-secret-key","s3-bucket","s3-region","s3-endpoint","agent","space"])^
    if (saved.version != 1 or saved.user != pairing.user(saved.user)^ or saved.folder != paths.folder)
        raise error("The saved sync profile uses a different user or folder.")
    if ((saved.agent != null and (not valid_token(saved.agent)^ or saved.space == null)) or
        (saved.space != null and not valid_space(saved.user,saved.space)^))
        raise error("The saved sync profile has an invalid agent or space.")
    return saved
}
pub pn prepare(paths, value, key) any^ {
    let selected = pairing.user(value)^
    let saved = load(paths)^
    if (saved != null and saved.user != selected) raise error("This folder already belongs to " ++ saved.user ++ ".")
    let target = directory(active(paths,saved),selected)^
    let space = if (saved == null or saved.space == null) pairing.space(selected)^ else saved.space
    io.mkdir(target)^
    io.mkdir(paths.folder)^
    if (not exists(target ++ "/device.sqlite")) {
        identity.save(target,key,identity.create(space,crypto.key()^)^)^
    }
    let credentials = identity.load(target,key)^
    if (credentials.space != space) raise error("The saved device belongs to a different user.")
    return credentials
}
pn finish(paths, value, space=null) any^ {
    let previous = load(paths)^
    // Preserve explicitly configured cloud credentials when a profile is recovered.
    let chosen = if (space == null) previous.space else space
    let saved = {*:if (previous == null) ({}) else previous,version:1,user:pairing.user(value)^,folder:paths.folder,
        *:if (chosen == null) ({}) else {space:chosen}}
    output(saved,paths.root ++ "/profile.json",{format:'json',atomic:true})^
    return saved
}
pub pn recover(paths, value, key) any^ {
    let saved = load(paths)^
    let target = directory(active(paths,saved),value)^
    if (not exists(target ++ "/repo.sqlite")) { return null }
    let opened = agent.open_agent(target,key)^
    if (local.status(opened.handle)^.state.root != disk.resolve(paths.folder)^)
        raise error("The saved repository uses a different sync folder.")
    return finish(paths,value)^
}
pub pn create(paths, value, key) any^ {
    let credentials = prepare(paths,value,key)^
    let saved = recover(paths,value,key)^
    if (saved != null) { return saved }
    let target = directory(active(paths,load(paths)^),value)^
    local.create(target ++ "/repo.sqlite",credentials.secret,credentials.space,paths.folder,credentials.device.id)^
    return finish(paths,value)^
}
pub pn enroll(paths, value, key, invitation, expected) any^ {
    let credentials = prepare(paths,value,key)^
    let selected = identity.enroll(credentials,invitation,expected)^
    if (not valid_space(value,selected.space)^) raise error("The invitation belongs to a different user.")
    agent.enroll(directory(active(paths,load(paths)^),value)^,key,paths.folder,credentials.device.id,invitation,expected)^
    return finish(paths,value,selected.space)^
}

// Storage settings are optional; the caller keeps the secret inside the sync worker.
pub fn storage(saved) any^ {
    if (saved["s3-access-key"] == null and saved["s3-secret-key"] == null) { return null }
    if (saved["s3-access-key"] == null or saved["s3-secret-key"] == null or saved["s3-bucket"] == null or
        saved["s3-region"] == null or saved["s3-endpoint"] == null)
        raise error("Sync cloud: complete the bucket, region and endpoint settings.")
    return {access_key:saved["s3-access-key"],secret_key:saved["s3-secret-key"],
        bucket:saved["s3-bucket"],region:saved["s3-region"],endpoint:saved["s3-endpoint"]}
}

// Explicit opt-in stages a fresh agent without touching the legacy repository.
pub pn migrate(paths, value, key) any^ {
    let saved = load(paths)^
    if (saved == null or saved.user != pairing.user(value)^ or saved.agent != null)
        raise error("Sync migration requires the original profile.")
    let legacy = identity.load(directory(paths,value)^,key) ^ { ^ }
    if (not (legacy is error) or legacy.message != "sync peer: legacy credentials require explicit re-enrollment; existing data preserved")
        raise error("Sync migration requires a legacy identity and the correct Keychain key.")
    let agent_id = hex.hex(crypto.key()^)^
    let space = pairing.space(value)^ ++ "-v2-" ++ agent_id
    let selected = {*:paths,agent:agent_id,space:space}
    let target = directory(selected,value)^
    if (exists(target)) raise error("Sync migration staging path already exists.")
    io.mkdir(target)^
    let credentials = identity.create(space,crypto.key()^)^
    identity.save(target,key,credentials)^
    let handle = local.create(target ++ "/repo.sqlite",credentials.secret,space,paths.folder,credentials.device.id)^
    local.scan(handle,null,true)^
    local.verify(handle)^
    if (not codec.same_encoding(load(paths)^,saved)^) raise error("Sync profile changed during migration; old profile remains selected.")
    let updated = {*:saved,agent:agent_id,space:space}
    output(updated,paths.root ++ "/profile.json",{format:'json',atomic:true})^
    return updated
}
