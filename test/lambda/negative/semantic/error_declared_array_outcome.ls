// S7.5.2v4/S7.8.2: plain arrays exclude an error replacing the whole array.
fn failed() int[]^ => error("array")
let values: int[] = failed()
values
