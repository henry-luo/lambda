// E228 accepts only immediate, explicit destinations for an enforcing result.
fn risky(value: int) int^ {
  if (value < 0) raise error("expected")
  else value
}

fn accept_error(value: int | error) int {
  match value {
    case error: 3
    case int: value
  }
}

fn binding_acknowledgment() int^ {
  let binding: int | error = risky(0 - 1)
  binding
}

fn or_acknowledgment() int {
  risky(0 - 1) or 2
}

fn parameter_acknowledgment() int {
  accept_error(risky(0 - 1))
}

fn match_acknowledgment() int {
  match risky(0 - 1) {
    case error: 4
    case int: 0
  }
}

fn tail_return() int^ {
  risky(0 - 1)
}

pn explicit_return() int^ {
  return risky(0 - 1)
}

[1, 2, 3, match_acknowledgment(), 5]
