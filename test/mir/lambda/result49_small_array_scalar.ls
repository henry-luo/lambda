pn fixed_reads() float {
    let values: float[] = [1.0, 2.0, 3.0]
    values[0] + values[2]
}

pn escape() float[] {
    let values: float[] = [4.0, 5.0]
    values
}

pn absent_read() any {
    let values: float[] = [6.0, 7.0]
    values[2]
}

pn main() {
    print(fixed_reads()); print(" ")
    print(escape()[0]); print(" ")
    print(absent_read() == null); print("\n")
}
