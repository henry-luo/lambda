import sh: lambda.shell

pn main() {
    ["first", "second"] |> sh.awk("{print; exit 7}")
                        |> upper(~) |> sh.sed("s/^/seen: /")
}
