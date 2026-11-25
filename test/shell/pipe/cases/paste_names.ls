import sh: lambda.shell

pn main() {
    sh.printf("%s\n", "name", "zebra", "apple", "pear")
        |> sh.tail("-n", "+2")
        |> sh.sort
        |> sh.paste("-s", "-d", ",", "-")
}
