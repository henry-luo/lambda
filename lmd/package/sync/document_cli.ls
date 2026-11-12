// headless editor entry point; credentials remain in the host's encrypted device store.
import host: .document_host
import peer: .peer_agent
import codec: .codec
import cli: .cli
import local: .local
import model: .document_state
import v: .schema
import disk: lambda.io.disk
import hex: lambda.security.tuf.metadata

pub pn execute(request, wrapping_key) any^ {
    let agent = peer.open_agent(request.directory,wrapping_key)^
    let handle = agent.handle
    let document = request.document
    let action = request.action
    if (action == "open") {
        return host.open_document(handle,request.path,symbol(request.profile or "text"),symbol(request.format or "text"))^
    }
    if (action == "edit") {
        let commands = if (request.commands_file != null) codec.decode(input(request.commands_file,'text')^)^
            else [for (command in request.commands) {*:command,kind:symbol(command.kind)}]
        return host.apply_edits(handle,document,commands,request.resolve or false,request.edit_id)^
    }
    if (action == "status") { return host.status(handle,document)^ }
    if (action == "refresh") { return host.refresh(handle,document)^ }
    if (action == "save") { return host.save(handle,document,request.save_id)^ }
    if (action == "prepare_save") { return host.prepare_save(handle,document,request.save_id)^ }
    if (action == "install_save") { host.install_save(handle,document)^; return host.status(handle,document)^ }
    if (action == "commit_save") { return host.commit_save(handle,document)^ }
    if (action == "cancel_save") { return host.cancel_save(handle,document)^ }
    if (action == "compact") { return host.compact(handle,document)^ }
    if (action == "restore_version") { return host.restore_version(handle,document,hex.unhex(request.version)^)^ }
    if (action == "attachment") {
        let captured = disk.capture(request.root,request.path,8388608)^
        return host.attachment(handle,captured.bytes,request.media_type or "application/octet-stream")^
    }
    if (action == "attach") {
        let captured = disk.capture(request.root,request.path,8388608)^
        let reference = host.attachment(handle,captured.bytes,request.media_type or "application/octet-stream")^
        return host.apply_edits(handle,document,[{kind:'blob',key:request.key,value:reference}])^
    }
    if (action == "read_attachment") {
        let value = host.status(handle,document)^.value
        let bytes = local.read_content(handle,value[request.key])^
        disk.install(request.root,request.path,bytes,null)^
        return {bytes:if (bytes == null) 0 else len(bytes),digest:hash(bytes,'sha256')^}
    }
    if (action == "versions") {
        return model.get(local.status(handle)^.state,document)^.session.versions
    }
    raise error("sync document: unknown editor action")
}
pn main() {
    return cli.invoke(pn (request,key,environment)=> { return execute(request,key)^ },"LAMBDA_SYNC_RESULT")^
}
