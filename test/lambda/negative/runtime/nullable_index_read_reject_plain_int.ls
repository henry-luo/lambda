// A total read has type int?. A plain int declaration must reject its OOB null
// dynamically rather than storing the nullable lane sentinel as an int value.
pn main() {
    var values: int[] = [7]
    var invalid: int = values[1]
    print(invalid)
}
