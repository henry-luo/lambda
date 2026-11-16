pn sleeper() any^ { sleep(1000)^; return true }
pn main() {
    let task = start(sleeper,[])
    let invalid = [for (value in [0.5,-1,"10"]) select(task,timeout:value) ^ { ^ }]
    let timeout = select(task,timeout:1) ^ { ^ }
    cancel(task); wait(task) ^ { null }
    return all([*[for (value in invalid) value.code == 312],timeout.code == 310])
}
