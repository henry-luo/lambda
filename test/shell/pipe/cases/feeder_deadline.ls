import sh: lambda.shell

pn main() { sh.cmd("sleep", ["30"], timeout_ms: 75) |> upper(~) |> sh.cat() }
