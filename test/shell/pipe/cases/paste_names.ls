import sh: lambda.shell

pn main() {
    sh.printf(null, "%s\n", "name", "zebra", "apple", "pear")
        |> sh.tail("-n", "+2")
        |> sh.sort
        |> sh.paste("-s", "-d", ",", "-")
}
