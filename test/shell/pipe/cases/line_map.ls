import sh: lambda.shell

pn main() { sh.stdin() |> sh.cat() |> upper(~) }
