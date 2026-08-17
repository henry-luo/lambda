// Tune18 follow-on E4.2: module-level typed string[] values retain their
// concrete array witness at an indexed read.

let GLOBAL_TABLE18: string[] = ["A", "B", "C"]

pn main() {
    print(GLOBAL_TABLE18[1])
}
