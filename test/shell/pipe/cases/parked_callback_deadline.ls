import sh: lambda.shell

pn delayed(line) { sleep(30000)^; line }
pn main() { ["apple"] |> sh.cat(timeout_ms: 75) |> delayed(~) }
