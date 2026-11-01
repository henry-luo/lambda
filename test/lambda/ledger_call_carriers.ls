// S7.7.3/D2.4.1: successful imported scalar calls retain their native carriers.
import native: .mod_ledger_long_names
fn step(x: u64) u64 => native.native_word(x) + 1u64;
fn uncertain(value) u64 { let admitted: u64 = value; admitted }
fn rejected_import(value) { let result = native.native_word(value); result is error };
[native.native_flag("ok") and true, step(2u64),
 native.native_word(18446744073709551615u64) + 1u64 == 0u64,
 bxor(2u64, uncertain("bad")) is error, bxor(2u64, uncertain(3)) == 1u64,
 rejected_import("bad")]
