// Test host supplies isolated paths and in-memory keys; production uses the OS vault.
import demo: ~~.mod_agent
import profile: ~~.mod_profile
import peer: lambda.sync.peer_agent
import cli: lambda.sync.cli
pn execute(request, key, environment) any^ {
    let paths = profile.paths(sys.proc.self.cwd#,request.profile,request.root)^
    let notify = pn (event)=> { output(event,request.events,{format:'json',atomic:true})^ }
    if (request.action == "host") { return demo.host(paths,request.user,key,notify,request.lifetime_ms or 120000)^ }
    if (request.action == "join") { return demo.join(paths,request.user,request.code,key,notify,request.lifetime_ms or 120000)^ }
    if (request.action == "run") { return demo.run(paths,key,notify,request.options or {})^ }
    if (request.action == "status") {
        let saved = profile.load(paths)^
        let result = peer.status(profile.directory(paths,saved.user)^,key)^
        output(result,request.events,{format:'json',atomic:true})^
        return true
    }
    raise error("test sync demo: unknown action")
}
pn main() { return cli.invoke(execute)^ }
