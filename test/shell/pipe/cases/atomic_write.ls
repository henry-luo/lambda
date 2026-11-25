import sh: lambda.shell

pn main() { (sh.stdin() |> sh.cat() |> output(\.'target.dat', {atomic: true}))^ }
