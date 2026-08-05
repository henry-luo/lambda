// Keep the direct float[] total-read path independently compilable.
pn main() {
    var values: float[] = [1.5]
    var absent: float? = values[1]
    print("ok\n")
}
