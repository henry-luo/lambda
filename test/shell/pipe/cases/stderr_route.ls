import sh: lambda.shell

pn main() { sh.cmd("cat", ["lambda-shell-test-missing-file"], stderr: \.'errors.log') |> sh.cat() }
