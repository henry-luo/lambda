// helper for the imported-procedure success-domain regression.
fn identity(value) any^ => value

pub pn produced(value, expected=null) {
    let prepared = identity(value)^
    if (expected != null) raise error("unexpected")
    return {*:prepared,source:{path:"test"}}
}
