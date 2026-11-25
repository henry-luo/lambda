import sh: lambda.shell

pn main() { b'6170706c650a' |> sh.grep("apple") }
