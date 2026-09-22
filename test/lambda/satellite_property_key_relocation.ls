// Each private image needs its own runtime-linked key suffix (D8.5.1v7).
// Computed keys prevent a static literal-shape guard from hiding a bad NameId.
fn read_left(row) => row.satellite_left
fn read_right(row) => row.satellite_right
fn check_pair(row) => read_left(row) == 17 and read_right(row) == 29
let left_key = "satellite_left"
let right_key = "satellite_right"
let row = {[left_key]: 17, [right_key]: 29}
all([for (i in 1 to 10000) check_pair(row)])
read_left(row)
read_right(row)
