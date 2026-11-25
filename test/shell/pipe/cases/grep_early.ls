import sh: lambda.shell

pn main() { sh.yes(null, "match") |> sh.grep("-q", "^match$") }
