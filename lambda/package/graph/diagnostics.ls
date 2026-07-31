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
  // Diagnostics must remain reportable when malformed metadata makes a
  // value-derived source location fail its checked conversion.
  make(code, severity, message, path, source_of(value)) or {
    code: "graph.diagnostic", severity: "error", message: "Graph diagnostic",
    path: null, source: null
  }

pub fn from_element(value) =>
  // A malformed parser diagnostic still needs a reportable graph.parse error.
  // Do not let optional metadata erase the complete graph normalization result.
  make(
    if (value.code != null) value.code else "graph.parse",
    if (value.severity != null) value.severity else "error",
    if (value.message != null) value.message else "Graph parse error",
    null,
    source_of(value)
  ) or {
    code: "graph.parse", severity: "error", message: "Graph parse error",
    path: null, source: null
  }

pub fn has_errors(values) =>
  len([for (value in values where value.severity == "error") value]) > 0
