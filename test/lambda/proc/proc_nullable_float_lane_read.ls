// A total float[] read must use FLOAT_LANE_NULL, not turn absence into 0.0.
pn main() {
    var values: float[] = [1.5]
    var present: float? = values[0]
    var absent: float? = values[1]
    print(string([present, absent]) ++ "\n")
}
