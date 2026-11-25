import sh: lambda.shell

pn child() any^ {
    sh.sleep(null, "30") |> \.'cancelled.dat'
}
pn main() {
    let child_task = start(child)
    sleep(50)^
    cancel(child_task)
    let result: any^ = wait(child_task) ^ { ^ };
    result is error
}
