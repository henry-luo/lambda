// decimal operations for the script TeX floating-point expression evaluator.
// a finite value is rounded to the expl3 precision after each operation.
fn ch(s, i) any^ => if (i >= len(s)) "" else slice(s, i, i + 1)^
fn digit(c) => c != "" and index_of("0123456789", c) != null
fn skip(s, i) any^ => if (ch(s, i)^ == " ") skip(s, i + 1) else i
fn zeroes(count, out = "") any^ => if (count <= 0) out else zeroes(count - 1, out ++ "0")
fn first_nonzero(s, i = 0) any^ => if (i >= len(s) or ch(s, i)^ != "0") i else first_nonzero(s, i + 1)
fn trim_zeros(s) any^ => if (len(s) > 0 and ch(s, len(s) - 1)^ == "0") trim_zeros(slice(s, 0, len(s) - 1)^) else s

fn parts(value) any^ {
    let raw = string(value);
    let negative = starts_with(raw, "-");
    let unsigned = if (negative) slice(raw, 1, len(raw))^ else raw;
    let e = index_of(upper(unsigned), "E");
    let mantissa = if (e == null) unsigned else slice(unsigned, 0, e)^;
    let exponent = if (e == null) 0 else int(slice(unsigned, e + 1, len(unsigned))^) ^ { 0 };
    let dot = index_of(mantissa, ".");
    {negative: negative, digits: replace(mantissa, ".", ""),
        point: if (dot == null) len(mantissa) else dot, exponent: exponent}
}

fn magnitude(value) any^ {
    let p = parts(value)^;
    let first = first_nonzero(p.digits)^;
    if (first == len(p.digits)) 0 else p.exponent + p.point - first - 1
}

// decimal ties are rounded to an even last digit, independently of Lambda's round().
fn nearest_even(value) any^ {
    let whole = trunc(value)^;
    let difference = abs(value - whole)^;
    let up = difference > 0.5m or (difference == 0.5m and abs(whole % 2m)^ == 1m);
    if (not up) whole else whole + if (value < 0m) -1m else 1m
}

fn places(value, digits) any^ {
    let power = 10m ** abs(digits);
    if (digits >= 0) nearest_even(value * power)^ / power
    else nearest_even(value / power)^ * power
}

fn normalize(value) any^ {
    if (value == 0m) value
    else {
        let exp = magnitude(value)^;
        if (exp < -200 or exp > 200) error("expl3-fp-range")
        else places(value, 15 - exp)
    }
}

fn square_root_step(x, current, previous, remaining) any^ {
    if (remaining <= 0) current
    else {
        let next = (current + x / current) / 2m;
        if (next == current or next == previous) next
        else square_root_step(x, next, current, remaining - 1)
    }
}

fn square_root(value) any^ {
    if (value < 0m) error("expl3-fp-domain")
    else if (value == 0m) value
    else {
        let exp = magnitude(value)^;
        let half = int(floor(exp / 2))^;
        let start = 10m ** half;
        normalize(square_root_step(value, start, 0m, 20)^)
    }
}

fn render(value) any^ {
    let p = parts(value)^;
    let first = first_nonzero(p.digits)^;
    if (first == len(p.digits)) if (p.negative) "-0" else "0"
    else {
        let digits = trim_zeros(slice(p.digits, first, len(p.digits))^) ^;
        let point = p.point + p.exponent - first;
        let body = if (point <= 0) "0." ++ zeroes(-point)^ ++ digits
            else if (point >= len(digits)) digits ++ zeroes(point - len(digits))^
            else slice(digits, 0, point)^ ++ "." ++ slice(digits, point, len(digits))^;
        (if (p.negative) "-" else "") ++ body
    }
}

fn span_digits(source, index) any^ => if (digit(ch(source, index)^)) span_digits(source, index + 1) else index
fn read_number(source, start) any^ {
    let integer_end = span_digits(source, start)^;
    let fraction_end = if (ch(source, integer_end)^ == ".") span_digits(source, integer_end + 1)^ else integer_end;
    let digits_present = integer_end > start or fraction_end > integer_end + 1;
    let e = ch(source, fraction_end)^;
    let has_exp = e == "e" or e == "E";
    let exp_start = fraction_end + if (has_exp) 1 else 0;
    let signed = ch(source, exp_start)^ == "+" or ch(source, exp_start)^ == "-";
    let exp_digits = exp_start + if (signed) 1 else 0;
    let end = if (has_exp) span_digits(source, exp_digits)^ else fraction_end;
    if (not digits_present or (has_exp and end == exp_digits)) {issue: "expl3-fp-number", index: start}
    else if ((integer_end - start) + (if (fraction_end > integer_end) fraction_end - integer_end - 1 else 0) > 30)
        {issue: "expl3-fp-range", index: start}
    else if (has_exp and (end - exp_digits > 3 or abs(int(slice(source, exp_start, end)^) ^ { 999 }) > 200))
        {issue: "expl3-fp-range", index: start}
    else {
        let value = decimal(slice(source, start, end)^) ^ { null };
        if (value == null) {issue: "expl3-fp-number", index: start}
        else {
            let rounded = normalize(value) ^ { null };
            if (rounded == null) {issue: "expl3-fp-range", index: start}
            else {issue: null, value: rounded, index: end}
        }
    }
}

fn ident_end(source, index) any^ {
    let c = ch(source, index)^;
    if (c != "" and ((c >= "a" and c <= "z") or (c >= "A" and c <= "Z"))) ident_end(source, index + 1) else index
}

fn primary(source, index) any^ {
    let at = skip(source, index)^;
    let c = ch(source, at)^;
    if (c == "(") {
        let nested = expression(source, at + 1)^;
        let end = skip(source, nested.index)^;
        if (nested.issue != null) nested
        else if (ch(source, end)^ != ")") {issue: "expl3-fp-missing-paren", index: end}
        else {*:nested, index: end + 1}
    }
    else if (digit(c) or c == ".") read_number(source, at)
    else {
        let end = ident_end(source, at)^;
        let name = slice(source, at, end)^;
        let opening = skip(source, end)^;
        if (name == "" or ch(source, opening)^ != "(") {issue: "expl3-fp-unsupported", unsupported: name, index: at}
        else call(source, name, opening + 1)
    }
}

fn call(source, name, start) any^ {
    let first = expression(source, start)^;
    let at = skip(source, first.index)^;
    if (first.issue != null) first
    else {
        let second = if (ch(source, at)^ == ",") expression(source, at + 1)^ else null;
        let end = if (second == null) at else skip(source, second.index)^;
        if (second != null and second.issue != null) second
        else if (ch(source, end)^ != ")") {issue: "expl3-fp-missing-paren", index: end}
        else if (name == "sqrt" and second == null) {
            let value = square_root(first.value) ^ { null };
            if (value == null) {issue: "expl3-fp-domain", index: end}
            else {issue: null, value: value, index: end + 1}
        }
        else if (name == "abs" and second == null)
            {issue: null, value: abs(first.value), index: end + 1}
        else if (name == "round" and (second == null or (second.value >= -200m and second.value <= 200m and second.value == trunc(second.value)^))) {
            let digits = if (second == null) 0 else int(second.value)^;
            let value = normalize(places(first.value, digits)^) ^ { null };
            if (value == null) {issue: "expl3-fp-range", index: end}
            else {issue: null, value: value, index: end + 1}
        }
        else {issue: "expl3-fp-unsupported", unsupported: name, index: start}
    }
}

fn unary(source, index) any^ {
    let at = skip(source, index)^;
    let c = ch(source, at)^;
    if (c == "+") unary(source, at + 1)
    else if (c == "-") {
        let result = unary(source, at + 1)^;
        if (result.issue != null) result else {*:result, value: -1m * result.value}
    }
    else primary(source, at)
}

fn term_tail(source, left) any^ {
    let at = skip(source, left.index)^;
    let operator = ch(source, at)^;
    if (left.issue != null or (operator != "*" and operator != "/")) left
    else {
        let right = unary(source, at + 1)^;
        if (right.issue != null) right
        else if (operator == "/" and right.value == 0m) {issue: "expl3-fp-division-by-zero", index: at}
        else {
            let value = normalize(if (operator == "*") left.value * right.value else left.value / right.value) ^ { null };
            if (value == null) {issue: "expl3-fp-range", index: at}
            else term_tail(source, {issue: null, value: value, index: right.index})
        }
    }
}
fn term(source, index) any^ => term_tail(source, unary(source, index)^)
fn expression_tail(source, left) any^ {
    let at = skip(source, left.index)^;
    let operator = ch(source, at)^;
    if (left.issue != null or (operator != "+" and operator != "-")) left
    else {
        let right = term(source, at + 1)^;
        if (right.issue != null) right
        else {
            let value = normalize(if (operator == "+") left.value + right.value else left.value - right.value) ^ { null };
            if (value == null) {issue: "expl3-fp-range", index: at}
            else expression_tail(source, {issue: null, value: value, index: right.index})
        }
    }
}
fn expression(source, index) any^ => expression_tail(source, term(source, index)^)

pub fn evaluate(source) any^ {
    let result = expression(source, 0)^;
    let end = skip(source, result.index)^;
    if (result.issue != null) result
    else if (end != len(source)) {issue: "expl3-fp-unsupported", unsupported: ch(source, end)^, index: end}
    else {issue: null, value: render(result.value)^}
}
