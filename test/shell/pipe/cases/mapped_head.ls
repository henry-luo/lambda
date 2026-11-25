import sh: lambda.shell

pn main() { sh.yes("test") |> upper(~) |> sh.head(lines: 1) }
