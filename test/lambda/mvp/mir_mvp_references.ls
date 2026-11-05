// S2.4.3v3/S2.4.4: address lookup and forcing use the shared document context.
pn force_value(value: any) any { value# }
fn address_value(value: any) any { &value }
pn main() {
    let original = temp('mvp_reference', {n: 7, rows: [1, 2]})^
    let loaded = temp.'mvp_reference'#
    print([loaded.n, original === loaded, address_value(loaded) != null]); print("\n")
    let address = address_value(loaded)
    let reread = force_value(address)
    print([reread.n, reread.rows, reread === original]); print("\n")
    let churn = temp('mvp_reference_other', {n: 9})^
    print([loaded.n, churn.n, address_value(17) is null, address_value([1, 2]) is null]); print("\n")
    let failed: any = shl(1, -1)
    print([force_value(17) is error, force_value(failed) is error,
           address_value(failed) is error]); print("\n")
}
