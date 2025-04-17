let g = 2;

fn multiple(a, b) {
    a * b
}

fn main() {
    // comment
    let a = 8 + 5, b = true, c = null, d = "hello", e = [1, 2, 3], h=["123", "true", "str"],
        f = g + a + 1, r=e[0], k = for (j in [1, 2], m in [3,4]) j+m,
        p = {a:b}, q = p.a, s = multiple(2, 3),
    if (f > 15 and q and r==1 and s==6) "great"
    else "not great"  
}
