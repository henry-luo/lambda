import sh: lambda.shell

pn main() { sh.yes("test") |> sh.head(lines: 1) }
