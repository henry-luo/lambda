// Safe Mermaid graph style parsing for semantic HTML and generated SVG paint.

fn empty_style() => {
  fill: null,
  stroke: null,
  stroke_width: null,
  color: null,
  opacity: null,
  dash_array: null
}

fn split_declarations_at(text, i, depth, escaped, current, parts) {
  if (i >= len(text)) parts ++ [current]
  else {
    let ch = slice(text, i, i + 1);
    if (escaped) split_declarations_at(text, i + 1, depth, false, current ++ ch, parts)
    else if (ch == "\\") split_declarations_at(text, i + 1, depth, true, current, parts)
    else if (ch == "(") split_declarations_at(text, i + 1, depth + 1, false, current ++ ch, parts)
    else if (ch == ")" and depth > 0)
      split_declarations_at(text, i + 1, depth - 1, false, current ++ ch, parts)
    else if ((ch == "," or ch == ";") and depth == 0)
      split_declarations_at(text, i + 1, depth, false, "", parts ++ [current])
    else split_declarations_at(text, i + 1, depth, false, current ++ ch, parts)
  }
}

fn split_declarations(raw) =>
  split_declarations_at(string(if (raw != null) raw else ""), 0, 0, false, "", [])

fn chars_are(text, allowed, i) {
  if (i >= len(text)) true
  else if (contains(allowed, slice(text, i, i + 1))) chars_are(text, allowed, i + 1)
  else false
}

fn valid_hex_color(text) {
  let count = len(text) - 1;
  (len(text) > 1) and slice(text, 0, 1) == "#" and
    contains([3, 4, 6, 8], count) and
    chars_are(slice(text, 1, len(text)), "0123456789abcdefABCDEF", 0)
}

fn valid_named_color(text) =>
  (len(text) > 0) and chars_are(text, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", 0)

fn functional_color_body(text, prefix) {
  let body_start = len(prefix);
  starts_with(lower(text), prefix) and ends_with(text, ")") and
    (body_start < len(text) - 1) and
    chars_are(slice(text, body_start, len(text) - 1), "0123456789.,% +-", 0)
}

fn valid_color(value) {
  let text = trim(value);
  valid_hex_color(text) or valid_named_color(text) or
    functional_color_body(text, "rgb(") or functional_color_body(text, "rgba(") or
    functional_color_body(text, "hsl(") or functional_color_body(text, "hsla(")
}

fn valid_unsigned_decimal_at(text, i, dots, digits) {
  if (i >= len(text)) { if (digits > 0) true else false }
  else {
    let ch = slice(text, i, i + 1);
    if (contains("0123456789", ch)) valid_unsigned_decimal_at(text, i + 1, dots, digits + 1)
    else if (ch == "." and dots == 0) valid_unsigned_decimal_at(text, i + 1, 1, digits)
    else false
  }
}

fn unsigned_number_text(value, allow_px) {
  let text = lower(trim(value));
  let numeric_text = if (allow_px and ends_with(text, "px")) trim(slice(text, 0, len(text) - 2))
    else text;
  if (valid_unsigned_decimal_at(numeric_text, 0, 0, 0)) numeric_text else null
}

fn safe_width(value) {
  let numeric_text = unsigned_number_text(value, true);
  if (numeric_text == null) null else float(numeric_text)
}

fn safe_opacity(value) {
  let numeric_text = unsigned_number_text(value, false);
  if (numeric_text == null) null
  else {
    let parsed = float(numeric_text);
    if (parsed >= 0.0 and parsed <= 1.0) parsed else null
  }
}

fn valid_dash_array(value) {
  let text = trim(value);
  (len(text) > 0) and chars_are(text, "0123456789., ", 0) and
    contains_digit(text, 0)
}

fn contains_digit(text, i) {
  if (i >= len(text)) false
  else if (contains("0123456789", slice(text, i, i + 1))) true
  else contains_digit(text, i + 1)
}

fn apply_declaration(state, property, value) {
  let key = lower(trim(property));
  let text = trim(value);
  if ((key == "fill" or key == "background" or key == "background-color") and valid_color(text)) {
    {*:state, fill: text}
  }
  else if ((key == "stroke" or key == "border-color") and valid_color(text)) {
    {*:state, stroke: text}
  }
  else if (key == "color" and valid_color(text)) { {*:state, color: text} }
  else if (key == "stroke-width" or key == "border-width") {
    let value = safe_width(text);
    if (value != null) { {*:state, stroke_width: value} } else state
  }
  else if (key == "opacity") {
    let value = safe_opacity(text);
    if (value != null) { {*:state, opacity: value} } else state
  }
  else if (key == "stroke-dasharray" and valid_dash_array(text)) { {*:state, dash_array: text} }
  else state
}

fn parse_declarations(parts, i, state) {
  if (i >= len(parts)) state
  else {
    let declaration = trim(parts[i]);
    let colon = index_of(declaration, ":");
    let next = if (colon <= 0) state
      else apply_declaration(state, slice(declaration, 0, colon),
        slice(declaration, colon + 1, len(declaration)));
    parse_declarations(parts, i + 1, next)
  }
}

pub fn parse(raw) => parse_declarations(split_declarations(raw), 0, empty_style())

pub fn node_css(parsed) =>
  (if (parsed.fill != null) "background:" ++ parsed.fill ++ ";" else "") ++
  (if (parsed.stroke != null) "border-color:" ++ parsed.stroke ++ ";" else "") ++
  (if (parsed.stroke_width != null) "border-width:" ++ string(parsed.stroke_width) ++ "px;" else "") ++
  (if (parsed.color != null) "color:" ++ parsed.color ++ ";" else "") ++
  (if (parsed.opacity != null) "opacity:" ++ string(parsed.opacity) ++ ";" else "")
