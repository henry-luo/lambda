import sh: lambda.shell

pn main() { sh.cmd("printf", ["%s\n", "two words", "literal;value"]) }
