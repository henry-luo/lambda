import sh: lambda.shell

pn main() { sh.ls() |> sh.head(lines: 1) }
