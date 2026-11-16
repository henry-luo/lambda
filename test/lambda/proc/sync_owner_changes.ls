import owner: lambda.sync.task_owner
fn observed(current) => current.value
pn main() {
    let broker = start(owner.run,[0,observed])
    let changed = start(owner.changed,[broker,0])
    let early = wait(changed,timeout:10) ^ { ^ }
    let rejected = owner.call(broker,pn ()=> { raise error("rollback") }) ^ { ^ }
    let held = owner.current(broker)^
    owner.call(broker,pn ()=> { return {state:{endpoint:{},value:1}} })^
    let first = wait(changed)^
    let immediate = owner.changed(broker,0)^
    for (i in 1 to 32) {
        let abandoned = start(owner.changed,[broker,1])
        wait(abandoned,timeout:1) ^ { null }
        cancel(abandoned); wait(abandoned) ^ { null }
    }
    let next = start(owner.changed,[broker,1])
    owner.call(broker,pn ()=> { return {state:{endpoint:{},value:2}} })^
    let second = wait(next)^
    send(broker,{stop:true})^; wait(broker)^
    return all([early.code == 310,rejected is error,held == 0,first == 1,immediate == 1,second == 2])
}
