// S7.5.3v2: `or` cannot acknowledge an enforcing channel.
fn risky() int^ => raise error("expected")
risky() or 7
