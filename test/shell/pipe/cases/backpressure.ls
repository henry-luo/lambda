import sh: lambda.shell

pn main() {
    sh.stdin() |> sh.cat()
        |> upper(~)
        |> sh.cat(timeout_ms: 3000)
}
