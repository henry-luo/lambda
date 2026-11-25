import sh: lambda.shell

pn select_codes(source, minimum) {
    source |> sh.cut("-d,", "-f2")
           |> int(trim(~)) |: ~ >= minimum
           |> sh.cmd("sort", ["-n"])
}
pn main() {
    let codes = sh.stdin() |> select_codes(400)
    codes |> sh.uniq(count: true)
          |> sh.awk("{print $2, $1}")
          |> "status " ++ ~
}
