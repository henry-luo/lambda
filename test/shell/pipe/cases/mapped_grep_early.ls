import sh: lambda.shell

pn main() { sh.yes(null, "match") |> lower(~) |> sh.grep("-q", "^match$") }
