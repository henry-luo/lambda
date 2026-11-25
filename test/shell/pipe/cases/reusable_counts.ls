import sh: lambda.shell

pn some_piping(source) {
    source |> sh.sort |> sh.uniq(count: true)
}
pn main() {
    ("pear", "apple", "pear") |> some_piping |> sh.head(lines: 1)
}
