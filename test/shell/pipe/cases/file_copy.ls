import sh: lambda.shell

pn main() { let dest = \.'target.dat'; \.'source.dat' |> sh.cat() |> dest }
