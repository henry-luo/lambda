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
// Section 1: ^expr operator (is_error check)
// ============================================

fn test_is_error_on_error() {
    let a^err = fail()
    ^err
}
test_is_error_on_error()

fn test_is_error_on_success() {
    let b^err = succeed()
    ^err
}
test_is_error_on_success()

fn test_is_error_on_int() {
    ^42
}
test_is_error_on_int()

fn test_is_error_on_string() {
    ^"hello"
}
test_is_error_on_string()

// ============================================
// Section 2: Error truthiness and defaults
// ============================================

fn test_error_is_falsy() {
    let a^err = fail()
    if (err) 1 else 0
}
test_error_is_falsy()

fn test_error_or_default() {
    let a^err = fail()
    err or 100
}
test_error_or_default()

fn test_success_or_default() {
    let a^err = succeed()
    a or 999
}
test_success_or_default()

// ============================================
// Section 3: type() and string() on error
// ============================================

fn test_type_of_error() {
    let a^err = fail()
    type(err)
}
test_type_of_error()

fn test_string_of_error() {
    let a^err = fail()
    string(err)
}
test_string_of_error()

// ============================================
// Section 4: Arithmetic error propagation
// ============================================

fn test_add_error() {
    let a^err = fail()
    type(err + 10)
}
test_add_error()

fn test_sub_error() {
    let a^err = fail()
    type(err - 5)
}
test_sub_error()

fn test_mul_error() {
    let a^err = fail()
    type(err * 3)
}
test_mul_error()

fn test_div_error() {
    let a^err = fail()
    type(err / 2)
}
test_div_error()

fn test_pow_error() {
    let a^err = fail()
    type(err ^ 2)
}
test_pow_error()

fn test_mod_error() {
    let a^err = fail()
    type(err % 3)
}
test_mod_error()

fn test_neg_error() {
    let a^err = fail()
    type(-err)
}
test_neg_error()

// ============================================
// Section 5: Numeric function error propagation
// ============================================

fn test_abs_error() {
    let a^err = fail()
    type(abs(err))
}
test_abs_error()

fn test_round_error() {
    let a^err = fail()
    type(round(err))
}
test_round_error()

fn test_floor_error() {
    let a^err = fail()
    type(floor(err))
}
test_floor_error()

fn test_ceil_error() {
    let a^err = fail()
    type(ceil(err))
}
test_ceil_error()

fn test_min1_error() {
    let a^err = fail()
    type(min(err))
}
test_min1_error()

fn test_max1_error() {
    let a^err = fail()
    type(max(err))
}
test_max1_error()

fn test_min2_error() {
    let a^err = fail()
    type(min(err, 10))
}
test_min2_error()

fn test_max2_error() {
    let a^err = fail()
    type(max(5, err))
}
test_max2_error()

fn test_sum_error() {
    let a^err = fail()
    type(sum(err))
}
test_sum_error()

fn test_avg_error() {
    let a^err = fail()
    type(avg(err))
}
test_avg_error()

fn test_int_error() {
    let a^err = fail()
    type(int(err))
}
test_int_error()

fn test_float_error() {
    let a^err = fail()
    type(float(err))
}
test_float_error()

// ============================================
// Section 6: Math function error propagation
// ============================================

fn test_sqrt_error() {
    let a^err = fail()
    type(sqrt(err))
}
test_sqrt_error()

fn test_log_error() {
    let a^err = fail()
    type(log(err))
}
test_log_error()

fn test_sin_error() {
    let a^err = fail()
    type(sin(err))
}
test_sin_error()

fn test_cos_error() {
    let a^err = fail()
    type(cos(err))
}
test_cos_error()

fn test_exp_error() {
    let a^err = fail()
    type(exp(err))
}
test_exp_error()

// ============================================
// Section 7: String function error propagation
// ============================================

fn test_trim_error() {
    let a^err = fail()
    type(trim(err))
}
test_trim_error()

fn test_trim_start_error() {
    let a^err = fail()
    type(trim_start(err))
}
test_trim_start_error()

fn test_trim_end_error() {
    let a^err = fail()
    type(trim_end(err))
}
test_trim_end_error()

fn test_lower_error() {
    let a^err = fail()
    type(lower(err))
}
test_lower_error()

fn test_upper_error() {
    let a^err = fail()
    type(upper(err))
}
test_upper_error()

fn test_contains_error() {
    let a^err = fail()
    type(contains(err, "x"))
}
test_contains_error()

fn test_starts_with_error() {
    let a^err = fail()
    type(starts_with(err, "x"))
}
test_starts_with_error()

fn test_replace_error() {
    let a^err = fail()
    type(replace(err, "a", "b"))
}
test_replace_error()

fn test_index_of_error() {
    let a^err = fail()
    type(index_of(err, "x"))
}
test_index_of_error()

fn test_split_error() {
    let a^err = fail()
    type(split(err, ","))
}
test_split_error()

// ============================================
// Section 8: Vector/collection function error propagation
// ============================================

fn test_reverse_error() {
    let a^err = fail()
    type(reverse(err))
}
test_reverse_error()

fn test_sort_error() {
    let a^err = fail()
    type(sort(err))
}
test_sort_error()

fn test_unique_error() {
    let a^err = fail()
    type(unique(err))
}
test_unique_error()

fn test_len_error() {
    let a^err = fail()
    type(len(err))
}
test_len_error()

// ============================================
// Section 9: ? propagation
// ============================================

fn test_propagate_success() int^ {
    let x = succeed()?
    x + 8
}
test_propagate_success()?

// ============================================
// Section 10: Chained error propagation
// ============================================

fn step1() int^ {
    raise error("step1 failed")
}

fn step2() int^ {
    let val = step1()?
    val + 10
}

fn test_chain_error() {
    let a^err = step2()
    ^err
}
test_chain_error()

fn step3() int^ {
    let val = succeed()?
    val * 2
}

fn step4() int^ {
    let val = step3()?
    val + 1
}

fn test_chain_success() {
    let a^err = step4()
    a
}
test_chain_success()
