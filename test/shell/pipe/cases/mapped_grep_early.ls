import sh: lambda.shell

pn main() { sh.yes("match") |> lower(~) |> sh.grep("-q", "^match$") }
