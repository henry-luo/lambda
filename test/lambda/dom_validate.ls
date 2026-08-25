// F3: constraint-validation predicates from the dom package, exercised
// headlessly. The engine-coupled half (reading a live control, writing
// :valid/:invalid) is covered by the UI-automation tests instead.
import validate: lambda.package.dom.validate

{
  email: [
    validate.value_is_email("user@example.com"),
    validate.value_is_email("no-at-sign.com"),
    validate.value_is_email("missing-dot@example"),
    validate.value_is_email("two@at@example.com"),
    validate.value_is_email("@leading.com")
  ],
  url: [
    validate.value_is_url("http://example.com"),
    validate.value_is_url("https://example.com"),
    validate.value_is_url("ftp://example.com"),
    validate.value_is_url("example.com")
  ],
  number: [
    validate.value_is_number("42"),
    validate.value_is_number("12.5"),
    validate.value_is_number("-3"),
    validate.value_is_number("12abc"),
    validate.value_is_number("abc"),
    validate.value_is_number("1-2")
  ]
}
