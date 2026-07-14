// Structured diagnostics shared by graph parsers and pure normalization.

fn source_value(value, key) =>
  if (value is element and value[key] != null) value[key] else null

pub fn source_of(value) => {
  start: source_value(value, "source-start"),
  end: source_value(value, "source-end"),
  line: source_value(value, "source-line"),
  column: source_value(value, "source-column")
}

pub fn make(code, severity, message, path = null, source = null) => {
  code: string(code),
  severity: string(severity),
  message: string(message),
  path: path,
  source: source
}

pub fn for_value(code, severity, message, path, value) =>
  make(code, severity, message, path, source_of(value))

pub fn from_element(value) => make(
  if (value.code != null) value.code else "graph.parse",
  if (value.severity != null) value.severity else "error",
  if (value.message != null) value.message else "Graph parse error",
  null,
  source_of(value)
)

pub fn has_errors(values) =>
  len([for (value in values where value.severity == "error") value]) > 0
