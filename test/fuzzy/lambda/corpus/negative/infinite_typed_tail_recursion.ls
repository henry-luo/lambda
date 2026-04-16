// Infinite tail recursion with typed parameter
// Tests that TCO iteration guard works with typed (int) parameters

fn loop_forever(n: int) int => loop_forever(n + 1)
loop_forever(0)
