// Public profile metadata stays outside the tree; the host supplies its vault-backed wrapping key.
import pairing: lambda.sync.pairing
import identity: lambda.sync.peer_identity
import agent: lambda.sync.peer_agent
import local: lambda.sync.local
import crypto: lambda.io.crypto
import v: lambda.sync.schema
import disk: lambda.io.disk

pub fn paths(home, root=null, folder=null) any^ {
    if (not (home is string) or not starts_with(home,"/")) raise error("Sync needs an absolute home directory.")
    let selected = {root:root or (home ++ "/Library/Application Support/Lambda Sync Demo"),
        folder:folder or (home ++ "/lambda-sync")}
    if (not starts_with(selected.root,"/") or not starts_with(selected.folder,"/") or
        selected.root == selected.folder or starts_with(selected.root,selected.folder ++ "/") or
        starts_with(selected.folder,selected.root ++ "/")) raise error("Keep the sync profile outside the synced folder.")
    return selected
}
pub fn directory(paths, value) string^ => paths.root ++ "/" ++ pairing.account(value)^
pub pn load(paths) any^ {
    if (not exists(paths.root ++ "/profile.json")) { return null }
    let saved = input(paths.root ++ "/profile.json",'json')^
    v.fields(saved,["version","user","folder"])^
    if (saved.version != 1 or saved.user != pairing.user(saved.user)^ or saved.folder != paths.folder)
        raise error("The saved sync profile uses a different user or folder.")
    return saved
}
pub pn prepare(paths, value, key) any^ {
    let selected = pairing.user(value)^
    let saved = load(paths)^
    if (saved != null and saved.user != selected) raise error("This folder already belongs to " ++ saved.user ++ ".")
    let target = directory(paths,selected)^
    io.mkdir(target)^
    io.mkdir(paths.folder)^
    if (not exists(target ++ "/device.sqlite")) {
        identity.save(target,key,identity.create(pairing.space(selected)^,crypto.key()^)^)^
    }
    let credentials = identity.load(target,key)^
    if (credentials.space != pairing.space(selected)^) raise error("The saved device belongs to a different user.")
    return credentials
}
pn finish(paths, value) any^ {
    let saved = {version:1,user:pairing.user(value)^,folder:paths.folder}
    output(saved,paths.root ++ "/profile.json",{format:'json',atomic:true})^
    return saved
}
pub pn recover(paths, value, key) any^ {
    let target = directory(paths,value)^
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
    let target = directory(paths,value)^
    local.create(target ++ "/repo.sqlite",credentials.secret,credentials.space,paths.folder,credentials.device.id)^
    return finish(paths,value)^
}
pub pn enroll(paths, value, key, invitation, expected) any^ {
    let credentials = prepare(paths,value,key)^
    let selected = identity.enroll(credentials,invitation,expected)^
    if (selected.space != pairing.space(value)^) raise error("The invitation belongs to a different user.")
    agent.enroll(directory(paths,value)^,key,paths.folder,credentials.device.id,invitation,expected)^
    return finish(paths,value)^
}
