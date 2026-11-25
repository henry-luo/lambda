import sh: lambda.shell

pn main() { sh.yes(null, "test") |> sh.cmd("lambda-shell-test-no-such-command", []) }
