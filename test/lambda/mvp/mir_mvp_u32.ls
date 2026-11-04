// S4.4, D2.4.3: packed arithmetic, full-width shift counts and failed producers.
fn add_words(a: u32, b: u32) u32 => a + b
fn sub_words(a: u32, b: u32) u32 => a - b
fn mul_words(a: u32, b: u32) u32 => a * b
fn shifted(a: u32, count: int) any => shl(a, count)
fn right_shifted(a: u32, count: int) any => shr(a, count)
fn admitted_word(value: int?) u32^ { let word: u32 = value; word }

pn main() {
    print("wrap: " ++ int(add_words(4294967295u32, 1u32)) ++ " " ++ int(sub_words(0u32, 1u32)) ++ " " ++ int(mul_words(4294967295u32, 2u32)) ++ "\n")
    print("shift: " ++ int(shifted(1u32, 31)) ++ " " ++ int(right_shifted(4294967295u32, 31)) ++ "\n")
    print("wide: " ++ int(shifted(1u32, 4294967296)) ++ " " ++ int(right_shifted(4294967295u32, 4294967296)) ++ "\n")
    print("negative: " ++ (type(shifted(1u32, -4294967296)) == error) ++ "\n")
    print("failed: " ++ (type(bor(shl(1u32, -1), 2u32)) == error) ++ "\n")
    print("mixed: " ++ int(bor(1u32, 4294967296)) ++ "\n")
    let word: u32 = 17u32
    print("convert: " ++ int(word) ++ " " ++ (float(word) == 17.0) ++ "\n")
    print("admit: " ++ int(admitted_word(0)) ++ " " ++ int(admitted_word(4294967295)) ++ " " ++ (type(admitted_word(-1)) == error) ++ " " ++ (type(admitted_word(4294967296)) == error) ++ " " ++ (type(admitted_word(null)) == error) ++ "\n")
}
