// Tune12-A fixture: exact int div/mod expressions keep the i64 lane on the hot arm.

pn lane_div(a: int, b: int) int {
    return a div b
}

pn lane_mod(a: int, b: int) int {
    return a % b
}

pn main() {
    print(string(lane_div(10, 3)) ++ "\n")
    print(string(lane_mod(-17, 5)) ++ "\n")
    print(string(lane_div(1, 0)) ++ "\n")
    print(string(lane_div(0, 0)) ++ "\n")
    print(string(lane_mod(7, 0)) ++ "\n")
}
