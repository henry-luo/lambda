// S7.5.2v4/S7.8.2: bindings and collected bodies retain their outcomes.
// S7.5.5: an overwritten procedure result still needs acknowledgement.
fn risky(x) int^ { if (x < 0) raise error("negative") else x }

let bound = risky(1)                        // inferred receiver acknowledges

let looped = [for (i in [1, 2]) risky(i)]   // per-element outcomes are retained

fn interior() {
    let a = risky(2)                        // captured outcomes need not be inspected
    5
}

pn main() {
    io.mkdir("./temp/never_created")        // E228: a `pn` statement
    var v = 0
    v = risky(3)                            // inferred assignment acknowledges
    print([bound, looped, interior(), v])
}
