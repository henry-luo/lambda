// Module that uses error destructuring at top level

fn might_fail(x) int^ {
    if (x < 0) raise error("negative input")
    else x * 10
}

pub safe_val^safe_err = might_fail(5)
pub fail_val^fail_err = might_fail(-1)
