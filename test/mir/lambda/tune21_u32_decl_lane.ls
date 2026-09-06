// T21-1e: `var n: u32 = <int lane>` admits the value through the inline u32
// range check; the declaration boundary must not box the lane and call
// lambda_type_check (crypto_sha1's rol/safe_add paid that on every round).
pn rol(num: int, cnt: int) int {
    var n: u32 = num
    return int(bor(shl(n, cnt), shr(n, 32 - cnt)))
}
pn safe_add(x: int, y: int) int {
    var ux: u32 = if (x < 0) { x + 4294967296 } else { x }
    var uy: u32 = if (y < 0) { y + 4294967296 } else { y }
    return int(ux + uy)
}
pn main() {
    print(rol(5, 3)); print(" "); print(safe_add(7, 9)); print("\n")
}
