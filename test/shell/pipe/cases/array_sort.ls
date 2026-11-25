import sh: lambda.shell

pn main() { ["pear", "apple"] |> sh.sort }
