// Infinite mutual recursion via function reference
// Neither function terminates - tests stack overflow protection

fn ping(n) => ping(n + 1)
fn pong(n) => pong(n + 1)
ping(0)
