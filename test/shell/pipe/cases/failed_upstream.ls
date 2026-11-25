import sh: lambda.shell

pn main() { sh.cmd("false", []) |> sh.cat() }
