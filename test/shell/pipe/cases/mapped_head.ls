import sh: lambda.shell

pn main() { sh.yes(null, "test") |> upper(~) |> sh.head(lines: 1) }
