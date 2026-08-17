// Test error propagation through runtime functions
// Verifies that error values propagate correctly (not crash/corrupt)
// through arithmetic, string, math, and vector functions.

// Helper: function that always raises an error
fn fail() int^ {
    raise error("test error")
}

// Helper: function that succeeds
fn succeed() int^ {
    42
}

// ============================================
// Section 1: is error operator
// ============================================

fn test_is_error_on_error() {
    let a = fail() ^ { ^ }
    a is error
}
test_is_error_on_error()

fn test_is_error_on_success() {
    let b = succeed() ^ { ^ }
    b is error
}
test_is_error_on_success()

fn test_is_error_on_int() {
    42 is error
}
test_is_error_on_int()

fn test_is_error_on_string() {
    "hello" is error
}
test_is_error_on_string()

// ============================================
// Section 2: Error truthiness and defaults
// ============================================

fn test_error_is_falsy() {
    let a = fail() ^ { ^ }
    if (a) 1 else 0
}
test_error_is_falsy()

fn test_error_or_default() {
    let a = fail() ^ { ^ }
    a or 100
}
test_error_or_default()

fn test_success_or_default() {
    let a = succeed() ^ { null }
    a or 999
}
test_success_or_default()

// ============================================
// Section 3: type() and string() on error
// ============================================

fn test_type_of_error() {
    let a = fail() ^ { ^ }
    type(a)
}
test_type_of_error()

fn test_string_of_error() {
    let a = fail() ^ { ^ }
    string(a)
}
test_string_of_error()

// ============================================
// Section 4: Arithmetic error propagation
// ============================================

fn test_add_error() {
    let a = fail() ^ { ^ }
    type(a + 10)
}
test_add_error()

fn test_sub_error() {
    let a = fail() ^ { ^ }
    type(a - 5)
}
test_sub_error()

fn test_mul_error() {
    let a = fail() ^ { ^ }
    type(a * 3)
}
test_mul_error()

fn test_div_error() {
    let a = fail() ^ { ^ }
    type(a / 2)
}
test_div_error()

fn test_pow_error() {
    let a = fail() ^ { ^ }
    type(a ** 2)
}
test_pow_error()

fn test_mod_error() {
    let a = fail() ^ { ^ }
    type(a % 3)
}
test_mod_error()

fn test_neg_error() {
    let a = fail() ^ { ^ }
    type(-a)
}
test_neg_error()

// ============================================
// Section 5: Numeric function error propagation
// ============================================

fn test_abs_error() {
    let a = fail() ^ { ^ }
    type(abs(a))
}
test_abs_error()

fn test_round_error() {
    let a = fail() ^ { ^ }
    type(round(a))
}
test_round_error()

fn test_floor_error() {
    let a = fail() ^ { ^ }
    type(floor(a))
}
test_floor_error()

fn test_ceil_error() {
    let a = fail() ^ { ^ }
    type(ceil(a))
}
test_ceil_error()

fn test_min1_error() {
    let a = fail() ^ { ^ }
    type(min(a))
}
test_min1_error()

fn test_max1_error() {
    let a = fail() ^ { ^ }
    type(max(a))
}
test_max1_error()

fn test_min2_error() {
    let a = fail() ^ { ^ }
    type(min(a, 10))
}
test_min2_error()

fn test_max2_error() {
    let a = fail() ^ { ^ }
    type(max(5, a))
}
test_max2_error()

fn test_sum_error() {
    let a = fail() ^ { ^ }
    type(sum(a))
}
test_sum_error()

fn test_avg_error() {
    let a = fail() ^ { ^ }
    type(avg(a))
}
test_avg_error()

fn test_int_error() {
    let a = fail() ^ { ^ }
    type(int(a))
}
test_int_error()

fn test_float_error() {
    let a = fail() ^ { ^ }
    type(float(a))
}
test_float_error()

// ============================================
// Section 6: Math function error propagation
// ============================================

fn test_sqrt_error() {
    let a = fail() ^ { ^ }
    type(math.sqrt(a))
}
test_sqrt_error()

fn test_log_error() {
    let a = fail() ^ { ^ }
    type(math.log(a))
}
test_log_error()

fn test_sin_error() {
    let a = fail() ^ { ^ }
    type(math.sin(a))
}
test_sin_error()

fn test_cos_error() {
    let a = fail() ^ { ^ }
    type(math.cos(a))
}
test_cos_error()

fn test_exp_error() {
    let a = fail() ^ { ^ }
    type(math.exp(a))
}
test_exp_error()

// ============================================
// Section 7: String function error propagation
// ============================================

fn test_trim_error() {
    let a = fail() ^ { ^ }
    type(trim(a))
}
test_trim_error()

fn test_trim_start_error() {
    let a = fail() ^ { ^ }
    type(trim_start(a))
}
test_trim_start_error()

fn test_trim_end_error() {
    let a = fail() ^ { ^ }
    type(trim_end(a))
}
test_trim_end_error()

fn test_lower_error() {
    let a = fail() ^ { ^ }
    type(lower(a))
}
test_lower_error()

fn test_upper_error() {
    let a = fail() ^ { ^ }
    type(upper(a))
}
test_upper_error()

fn test_contains_error() {
    let a = fail() ^ { ^ }
    type(contains(a, "x"))
}
test_contains_error()

fn test_starts_with_error() {
    let a = fail() ^ { ^ }
    type(starts_with(a, "x"))
}
test_starts_with_error()

fn test_replace_error() {
    let a = fail() ^ { ^ }
    type(replace(a, "a", "b"))
}
test_replace_error()

fn test_index_of_error() {
    let a = fail() ^ { ^ }
    type(index_of(a, "x"))
}
test_index_of_error()

fn test_split_error() {
    let a = fail() ^ { ^ }
    type(split(a, ","))
}
test_split_error()

// ============================================
// Section 8: Vector/collection function error propagation
// ============================================

fn test_reverse_error() {
    let a = fail() ^ { ^ }
    type(reverse(a))
}
test_reverse_error()

fn test_sort_error() {
    let a = fail() ^ { ^ }
    type(sort(a))
}
test_sort_error()

fn test_unique_error() {
    let a = fail() ^ { ^ }
    type(unique(a))
}
test_unique_error()

fn test_len_error() {
    let a = fail() ^ { ^ }
    type(len(a))
}
test_len_error()

// ============================================
// Section 9: ? propagation
// ============================================

fn test_propagate_success() int^ {
    let x = succeed()^
    x + 8
}
test_propagate_success()^

// ============================================
// Section 10: Chained error propagation
// ============================================

fn step1() int^ {
    raise error("step1 failed")
}

fn step2() int^ {
    let val = step1()^
    val + 10
}

fn test_chain_error() {
    let a = step2() ^ { ^ }
    a is error
}
test_chain_error()

fn step3() int^ {
    let val = succeed()^
    val * 2
}

fn step4() int^ {
    let val = step3()^
    val + 1
}

fn test_chain_success() {
    let a = step4() ^ { null }
    a
}
test_chain_success()
