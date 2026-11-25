import sh: lambda.shell

pn main() { sh.cmd("sh", ["fragmented_stdout.sh"]) |> upper(~) }
