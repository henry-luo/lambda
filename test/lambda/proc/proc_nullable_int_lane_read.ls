// A total int[] read stays in the IntLane. The OOB arm is INT_LANE_NULL,
// which must box as null and propagate through native integer arithmetic.
pn main() {
    var values: int[] = [7]
    var present: int? = values[0]
    var absent: int? = values[1]
    print(string([present, absent, absent + 1, 1 + absent]) ++ "\n")
}
