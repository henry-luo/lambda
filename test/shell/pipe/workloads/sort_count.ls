import sh: lambda.shell

pn main() {
    sh.cut("-d,", "-f2", "input.csv") |> sh.sort |> sh.uniq(count: true)
}
