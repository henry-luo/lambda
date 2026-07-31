// An unannotated fn has the implicit `any \\ error` result contract. A known
// T^ tail call must be contained or propagated before it crosses the firewall.
fn may_fail(value: int) int^ {
  if (value < 0) raise error("negative")
  else value
}

fn leaks_error() {
  may_fail(0 - 1)
}

leaks_error()
