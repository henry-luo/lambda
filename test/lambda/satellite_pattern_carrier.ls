// Named patterns keep their type-list carrier after a function promotes from T0.
type operation = \("\a"+ "(" "\d"+ ")")
fn matches(source) => [find(source,operation) |> ~.value, source is operation];
let results=[for (i in 0 to 199) matches("rotate(90)")];
all([for (result in results) result[0]==["rotate(90)"] and result[1]])
