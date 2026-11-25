import sh: lambda.shell

pn main() { sh.git("log", "-1", "--oneline") }
