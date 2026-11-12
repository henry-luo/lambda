// lambda demo sync: document-owned LAN pairing and replication (S13.1.2v2, D11.2.3).
import dom
import crypto: lambda.io.crypto
import secret: lambda.io.secret
import hex: lambda.security.tuf.metadata
import pairing: lambda.sync.pairing
import profile: .mod_profile
import agent: .mod_agent
import model: .mod_model
import ui: .mod_ui
import resources: ~~.mod_resources
let ENV = sys.proc.self.env#
let PATHS = profile.paths(ENV.HOME,ENV.SYNC_DEMO_ROOT,ENV.SYNC_DEMO_DIR)^
let SERVICE = "Lambda Sync Demo (" ++ PATHS.root ++ ")"
let INITIAL = model.initial(PATHS.folder)^
let CSS = input(url_resolve(resources.entry_uri()^,"sync.css"),'text')^

pn report(root, event) {
    let target = dom.get_element_by_id(root,"sync-demo")
    if (target != null) dom.dispatch(target,{type:"sync_event",bubbles:false,detail:event})
}
pn wrapping_key(value) any^ {
    let account = pairing.account(value)^
    let saved = secret.get(SERVICE,account)^
    if (saved != null) { return hex.unhex(saved)^ }
    if (exists(profile.directory(PATHS,value)^ ++ "/device.sqlite"))
        raise error("The device key is unavailable. Unlock the login Keychain and try again.")
    let key = crypto.key()^
    secret.set(SERVICE,account,hex.hex(key)^)^
    return key
}
pn perform(root, action, value, code, notify) any^ {
    let saved = profile.load(PATHS)^
    if (action == "resume" and saved == null) { notify({type:"loaded"})^; return null }
    let user = if (action == "resume") saved.user else pairing.user(value)^
    if (saved != null and saved.user != user) raise error("This folder already belongs to " ++ saved.user ++ ".")
    if (action == "join") pairing.code(code)^
    let key = wrapping_key(user)^
    if (action == "host") agent.host(PATHS,user,key,notify)^
    if (action == "join") agent.join(PATHS,user,code,key,notify)^
    return agent.run(PATHS,key,notify)^
}
pn work(root, action, value=null, code=null) {
    let job = self()
    let notify = pn (event)=> { report(root,{*:event,job:job})^ }
    let result = perform(root,action,value,code,notify) ^ { ^ }
    if (result is error and result.code != 319) notify({type:"error",message:result.message})^
}
pn stop(worker) {
    if (worker != null) { cancel(worker); wait(worker) ^ { null } }
}
view <sync_app> state st:INITIAL, worker:null {
    <body ui.page(st)>
}
on load(evt) {
    worker = start(work,[dom.root_node(evt.target),"resume"],{owner:'document'})
    return 'handled'
}
on sync_event(evt) {
    if (evt.detail.job != worker) { return 'handled' }
    st = model.update(st,evt.detail)^
    return 'handled'
}
on input(evt) {
    let id = dom.get_attribute(evt.target,"id")
    if (id == "user-id") { st = {*:st,user:dom.get_state(evt.target,"value") or ""}; return 'handled' }
    if (id == "pair-code") { st = {*:st,input_code:dom.get_state(evt.target,"value") or ""}; return 'handled' }
    return 'pass'
}
on click(evt) {
    let button = dom.closest(evt.target,"[data-action]")
    if (button == null) { return 'pass' }
    let action = dom.get_attribute(button,"data-action")
    if (action == "folder") { io.exec("/usr/bin/open",[PATHS.folder],{timeout_ms:5000})^; return 'handled' }
    if (action == "copy-code") { dom.clipboard_write(st.pair_code)^; return 'handled' }
    if (action == "show-join") { st = {*:st,mode:"join",note:null}; return 'handled' }
    if (action == "back") { st = {*:st,mode:"setup",input_code:"",note:null}; return 'handled' }
    stop(worker)^
    worker = null
    if (action == "cancel") {
        let saved = profile.load(PATHS)^
        st = {*:st,mode:if (saved == null) "setup" else "paused",pair_code:null,input_code:"",note:null}
        return 'handled'
    }
    if (action == "pause") { st = {*:st,mode:"paused",pair_code:null}; return 'handled' }
    if (action == "setup") { st = {*:st,mode:"setup",pair_code:null,note:null}; return 'handled' }
    let operation = if (action in ["create","pair"]) "host" else if (action == "join") "join" else "resume"
    st = {*:st,mode:"starting",pair_code:null,note:null}
    worker = start(work,[dom.root_node(evt.target),operation,st.user,st.input_code],{owner:'document'})
    return 'handled'
}
<html lang:"en",<head <meta charset:"UTF-8"><meta name:"viewport",content:"width=device-width, initial-scale=1">
    <title "Sync — Lambda"><style CSS>> apply(<sync_app>)>
