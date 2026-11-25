import sh: lambda.shell

pn main() { (sh.cmd("false", []) |> output(\.'target.dat', {atomic: true}))^ }
