// test-only storage faults are confined to the authenticated agent's random test space.
import cli: lambda.sync.cli
import peer: lambda.sync.peer_agent
import s3: lambda.sync.s3
import schema: lambda.sync.cloud_schema
pn execute(request, key, environment) any^ {
    let opened = peer.open_agent(request.directory,key)^
    let config = parse(environment.LAMBDA_SCALE_STORAGE,'json')^
    let prefix = schema.prefix(opened.credentials.space)^
    if (request.action == "enumerate") {
        let found = s3.enumerate(config,prefix ++ request.prefix,request.cursor,64)^
        return {entries:[for (entry in found.entries) {*:entry,key:slice(entry.key,len(prefix)^)^}],next:found.next}
    }
    if (request.action == "erase") { return s3.erase(config,prefix ++ request.key)^ }
    let previous = s3.get(config,prefix ++ request.key)^
    if (request.action == "corrupt") {
        output(previous.body,request.stash,{format:'text',atomic:true})^
        return s3.write(config,prefix ++ request.key,"corrupt ciphertext",previous.etag)^
    }
    if (request.action == "restore") {
        return s3.write(config,prefix ++ request.key,input(request.stash,'text')^,previous.etag)^
    }
    raise error("scale admin: unsupported action")
}
pn main() { return cli.invoke(execute,"LAMBDA_CLOUD_RESULT")^ }
