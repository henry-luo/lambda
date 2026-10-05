// S10.1.2v4, D5.1.3: an injected call must keep the ordinary suspension boundary.
pn delayed(value: int) int | error {
    sleep(1)^
    value
}
pn main() {
    let result = (9 |> delayed())^
    print(result)
}
