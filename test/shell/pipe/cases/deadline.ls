import sh: lambda.shell

pn main() { sh.sleep("30") |> sh.cat(timeout_ms: 75) }
