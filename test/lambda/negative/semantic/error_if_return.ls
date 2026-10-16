// S3.4/S3.5/S5.1.5: the inferred return must retain a visited error.
fn rejected(value: string) => if (int(value)) 1 else 2
rejected("bad")
