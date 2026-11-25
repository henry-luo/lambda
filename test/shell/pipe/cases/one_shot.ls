import sh: lambda.shell

pn main() {
    let output = sh.yes(null, "test")
    let first = output |> sh.head(lines: 1)
    output |> sh.head(lines: 1)
}
