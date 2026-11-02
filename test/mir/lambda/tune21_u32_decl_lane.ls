// T21-1e: `var n: u32 = <int lane>` admits the value through the inline u32
// range check; the declaration boundary must not box the lane and call
// lambda_type_check (crypto_sha1's rol/safe_add paid that on every round).
pn rol(num: int, cnt: int) int^ {
    let _admitted_n_5: u32 = num
    var n: u32 = _admitted_n_5^
    return int(bor(shl(n, cnt), shr(n, 32 - cnt)))
}
pn safe_add(x: int, y: int) int^ {
    let _admitted_ux_9: u32 = if (x < 0) { x + 4294967296 } else { x }
    var ux: u32 = _admitted_ux_9^
    let _admitted_uy_10: u32 = if (y < 0) { y + 4294967296 } else { y }
    var uy: u32 = _admitted_uy_10^
    return int(ux + uy)
}
pn main() {
    print(rol(5, 3)); print(" "); print(safe_add(7, 9)); print("\n")
}
