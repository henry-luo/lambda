// kill after one authorized physical deletion but before the enclosing SQLite scope commits.
import agent: lambda.sync.peer_agent
import repo: lambda.sync.repository
import documents: lambda.sync.documents
import maintenance: lambda.sync.maintenance
import codec: lambda.sync.codec
import hex: lambda.security.tuf.metadata

pn main() {
    let environment = sys.proc.self.env#
    let request = input(environment.LAMBDA_SYNC_REQUEST,'json')^
    let owner = agent.open_agent(request.directory,hex.unhex(environment.LAMBDA_SYNC_KEY)^)^
    return repo.access(owner.handle.path,owner.handle.keys,pn (tx,k)=> {
        let loaded = repo.load(tx,k)^
        let current = documents.expand(tx,k,loaded.state)^
        let plan = current.maintenance
        if (plan == null or plan.kind != 'prune' or plan.id != hex.unhex(request.id)^ or
            plan.fingerprint != maintenance.fingerprint(current)^ or plan.next >= len(plan.objects)^)
            raise error("test retention: expected an authorized pending prune")
        // the real next-head write holds the SQLite writer lock across the injected crash window.
        repo.commit_head(tx,k,loaded.generation,{*:loaded.state,previous:loaded.id,maintenance:{*:plan,next:plan.next+1}})^
        repo.remove_object(tx,k,plan.objects[plan.next])^
        output("removed",request.ready,{format:'text',atomic:true})^
        sleep(300000)^
        return null
    })^
}
