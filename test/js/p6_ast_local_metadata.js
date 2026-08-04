// p6 keeps inferred local types on AST declarators.  This exercises a source
// name longer than the retired 128-byte table slot and more than 32 locals.
function p6AstLocalMetadata(seed) {
    let local_name_longer_than_the_retired_p6_fixed_table_slot_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz = seed + 1;
    let p6_01 = local_name_longer_than_the_retired_p6_fixed_table_slot_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz + 1;
    let p6_02 = p6_01 + 1;
    let p6_03 = p6_02 + 1;
    let p6_04 = p6_03 + 1;
    let p6_05 = p6_04 + 1;
    let p6_06 = p6_05 + 1;
    let p6_07 = p6_06 + 1;
    let p6_08 = p6_07 + 1;
    let p6_09 = p6_08 + 1;
    let p6_10 = p6_09 + 1;
    let p6_11 = p6_10 + 1;
    let p6_12 = p6_11 + 1;
    let p6_13 = p6_12 + 1;
    let p6_14 = p6_13 + 1;
    let p6_15 = p6_14 + 1;
    let p6_16 = p6_15 + 1;
    let p6_17 = p6_16 + 1;
    let p6_18 = p6_17 + 1;
    let p6_19 = p6_18 + 1;
    let p6_20 = p6_19 + 1;
    let p6_21 = p6_20 + 1;
    let p6_22 = p6_21 + 1;
    let p6_23 = p6_22 + 1;
    let p6_24 = p6_23 + 1;
    let p6_25 = p6_24 + 1;
    let p6_26 = p6_25 + 1;
    let p6_27 = p6_26 + 1;
    let p6_28 = p6_27 + 1;
    let p6_29 = p6_28 + 1;
    let p6_30 = p6_29 + 1;
    let p6_31 = p6_30 + 1;
    let p6_32 = p6_31 + 1;
    return p6_32;
}

console.log(p6AstLocalMetadata(0));
