// An ordinary expression cannot hide a T^ call from E228.
fn risky() int^ {
  raise error("expected")
}

risky() + 1
