import owner: lambda.sync.task_owner

fn observe(value) any^ {
    if (value.fail) raise error("observer failed")
    return value.endpoint
}
pn main() {
    let initial = {id:"old"}
    let task = start(owner.run,[{version:0},observe,initial])
    let rejected = owner.cached(task,pn (cache)=> {
        return {state:{endpoint:{version:1},fail:true},repository_cache:{id:"new"}}
    }) ^ { ^ }
    let after = owner.cached(task,pn (cache)=> { return {seen:cache.id} })^
    let token = owner.current(task)^
    send(task,{stop:true})^; wait(task)^
    return [rejected is error,after.seen == "old",token.version == 0]
}
