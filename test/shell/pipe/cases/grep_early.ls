import sh: lambda.shell

pn main() { sh.yes("match") |> sh.grep("-q", "^match$") }
