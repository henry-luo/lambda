// An implicit/typed parameter returns an incoming error before its body runs.
import .type_contract_metadata_module

fn source_fail(value: int) int^ {
  if (value < 0) raise error("source")
  else value
}

fn source_value_declared(value: int) int | error { value }

fn implicit_body(value) any { 111 }
fn typed_body(value: int) any { 444 }
fn typed_int_body(value: int) int { 555 }
fn explicit_any_body(value: any) any { 222 }
fn explicit_error_body(value: int | error) any { 223 }
fn shorthand_error_body(value: int | error) any { 224 }
fn optional_body(value: int = 999) any { 666 }
fn variadic_body(value: int, ...) any { 777 }

fn make_closure() any {
  let captured = 888
  fn short_circuit_closure(value: int) any => captured
  short_circuit_closure
}

fn tail_body(steps: int, value) any {
  if (steps == 0) 333
  else tail_body(steps - 1, source_fail(0 - 1))
}

let dynamic_implicit = implicit_body
let dynamic_typed = typed_body
let dynamic_error = explicit_error_body
let dynamic_shorthand = shorthand_error_body
let dynamic_optional = optional_body
let closure_body = make_closure()
let imported_body = imported
let accepted_value: int | error = source_value_declared(45)

[
  implicit_body(source_fail(0 - 1)) or 10,
  dynamic_implicit(source_fail(0 - 1)) or 20,
  typed_body(source_fail(0 - 1)) or 30,
  typed_int_body(source_fail(0 - 1)) or 35,
  dynamic_typed(source_fail(0 - 1)) or 40,
  explicit_any_body(error("source")),
  explicit_error_body(source_fail(0 - 1)),
  dynamic_error(source_fail(0 - 1)),
  shorthand_error_body(source_fail(0 - 1)),
  dynamic_shorthand(source_fail(0 - 1)),
  accepted_value or 46,
  optional_body(source_fail(0 - 1)) or 50,
  dynamic_optional(source_fail(0 - 1)) or 60,
  variadic_body(source_fail(0 - 1), 2) or 70,
  closure_body(error("source")) or 80,
  imported_body(source_fail(0 - 1)) or 85,
  tail_body(1, 0) or 90
]
