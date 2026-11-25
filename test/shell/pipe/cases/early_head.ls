import sh: lambda.shell

pn main() { sh.yes(null, "test") |> sh.head(lines: 1) }
