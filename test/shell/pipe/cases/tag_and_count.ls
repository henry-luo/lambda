import sh: lambda.shell

pn main() {
    \.'catalog.csv' |> sh.cut("-d,", "-f2")
        |> sh.tr("[:lower:]", "[:upper:]")
        |> sh.sed("s/^/tag:/")
        |> sh.tee("tagged items.txt")
        |> sh.wc("-l")
        |> int(trim(~))
}
