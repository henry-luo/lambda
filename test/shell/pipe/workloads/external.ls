import sh: lambda.shell

pn main() {
    sh.grep(null, "^ERROR,", "input.csv")
        |> sh.cut("-d,", "-f2")
        |> sh.tr("[:lower:]", "[:upper:]")
}
