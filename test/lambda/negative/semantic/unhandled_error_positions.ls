// LR10-9 (S7.5.1, S7.5.2): E228 holds in every position, not only in a
// top-level expression statement. Each marked call is unengaged.
fn risky(x) int^ { if (x < 0) raise error("negative") else x }

let bound = risky(1)                        // E228: a bare `let` never acknowledges

let looped = [for (i in [1, 2]) risky(i)]   // E228: a `for` body

fn interior() {
    let a = risky(2)                        // E228: inside an `fn`
    5
}

pn main() {
    io.mkdir("./temp/never_created")        // E228: a `pn` statement
    var v = 0
    v = risky(3)                            // E228: an unannotated reassignment
    print([bound, looped, interior(), v])
}
