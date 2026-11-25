import sh: lambda.shell

pn main() {
    ["INFO ready"] |> sh.grep("^ERROR")
                   |> upper(~) |> sh.sed("s/^/log: /")
}
