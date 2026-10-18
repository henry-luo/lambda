// S3.4/S3.5/S5.1.5: the inferred return must retain a visited error.
fn rejected(value: string) => int(value) or 7
rejected("bad")
