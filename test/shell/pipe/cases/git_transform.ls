import sh: lambda.shell

pn main() { sh.git("log", "-1", "--format=%h") |> upper(~) |> sh.cut("-c", "1-4") }
