import sh: lambda.shell

pn main() {
    sh.cut(null, "-d,", "-f2", "input.csv") |> sh.sort |> sh.uniq(count: true)
}
