// S2.6.6v3: each handler owns its producer's partial bytes, including loops.
fn fragments(count) => for (index in 0 to (count - 1))
    (string(index), if (index == 1) error("stopped") else ".")
fn recovered(count) any^ => fragments(count) ^ { ^.message }
fn array_value() => ["a", "b"]
fn captured(message) => () error => error(message)
let closure = captured("captured");
let saved = ["a", "b"];
let first = <file format:'pdf', "[" recovered(3)^ "]">;
let nested = <file format:'pdf', ("before", "outer", (("discarded", 42) ^ {"inner"}), "tail")>;
let failed = <file format:'pdf', ("before", "discarded", 42)> ^ { ^.message };
[binary(first) == binary("[stopped]"),
 binary(<file format:'pdf', fragments(1)>) == binary("0."),
 binary(nested) == binary("beforeouterinnertail"),
 failed is string,
 (<file format:'pdf', saved> ^ {true}) == true,
 (<file format:'pdf', array_value()> ^ {true}) == true,
 binary(<file format:'pdf', closure() ^ { ^.message }>) == binary("captured")]
