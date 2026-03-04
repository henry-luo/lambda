// Test len() dispatch and Phase 5 native len variants
// Verifies fn_len_s for typed string params, and generic len for collections

// ============================================
// Section 1: len with typed string param (fn_len_s)
// ============================================

fn str_len(s: string) { len(s) }

"1. Typed string len"
[
    str_len("hello"),
    str_len("Lambda Script"),
    str_len("日本語")
]

// ============================================
// Section 2: len on inline collections (generic fn_len)
// ============================================

"2. Inline collection len"
[
    len([1, 2, 3, 4]),
    len([10]),
    len((1, 2, 3)),
    len({a: 1, b: 2, c: 3})
]

// ============================================
// Section 3: len in expressions
// ============================================

fn is_empty_str(s: string) { len(s) == 0 }
fn str_has_content(s: string) { (len(s) > 0) }

"3. len in boolean expressions"
[
    is_empty_str("hello"),
    is_empty_str("x"),
    str_has_content("test"),
    str_has_content("a")
]

// ============================================
// Section 4: len in arithmetic
// ============================================

"4. len in arithmetic"
let arr = [10, 20, 30]
[sum(arr) + len(arr), len(arr) * 10]

// ============================================
// Section 5: len with untyped fallback (generic fn_len)
// ============================================

fn generic_len(x) { len(x) }

"5. Untyped len fallback"
[
    generic_len([1, 2, 3]),
    generic_len("hello"),
    generic_len({a: 1, b: 2})
]

// ============================================
// Section 6: len result used as typed int
// ============================================

fn repeat_char(s: string, n: int) {
    let l = len(s)
    l + n
}

"6. len result as int"
[repeat_char("abc", 10), repeat_char("hello world", 0)]

// ============================================
// Section 7: Chained len operations
// ============================================

fn longer(a: string, b: string) {
    if (len(a) >= len(b)) a else b
}

"7. Chained len"
[longer("short", "longer string"), longer("hello world", "hi")]

// ============================================
// Section 8: len on element
// ============================================

"8. Element len"
let el = <div; "a"; "b"; "c">
len(el)
