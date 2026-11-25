import sh: lambda.shell

pn main() { sh.yes("test") |> sh.cmd("lambda-shell-test-no-such-command", []) }
