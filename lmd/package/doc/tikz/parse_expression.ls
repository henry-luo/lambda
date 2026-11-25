import scan: lambda.io.source_scan

fn spaces(s, start: int) int^ {
    var pos = start
    while (true) {
        let b = scan.byte(s, pos)^
        if (not (scan.space(b) or b in [11,12])) { break }
        pos = pos + 1
    }
    return pos
}

fn hex_digit(b: int) bool => scan.digit(b) or (b >= 65 and b <= 70) or (b >= 97 and b <= 102)

fn digits(s, start: int, hexadecimal: bool = false) int^ {
    var pos = start
    while (true) {
        let b = scan.byte(s, pos)^
        if (not (if (hexadecimal) hex_digit(b) else scan.digit(b))) { break }
        pos = pos + 1
    }
    return pos
}

fn significand(s, start: int, hexadecimal: bool) int^ {
    let integer_stop = digits(s, start, hexadecimal)^
    if (scan.byte(s, integer_stop)^ != 46) { return integer_stop }
    let fraction_stop = digits(s, integer_stop + 1, hexadecimal)^
    return if (integer_stop == start and fraction_stop == integer_stop + 1) start else fraction_stop
}

fn exponent_end(s, stop: int, markers: int[]) int^ {
    if (not (scan.byte(s, stop)^ in markers)) { return stop }
    let first = stop + 1
    let digits_at = if (scan.byte(s, first)^ in [43,45]) first + 1 else first
    let scanned = digits(s, digits_at)^
    return if (scanned == digits_at) stop else scanned
}

pub fn number_end(s, start: int) int^ {
    if (scan.byte(s, start)^ == 48 and scan.byte(s, start + 1)^ in [88,120]) {
        let stop = significand(s, start + 2, true)^
        if (stop > start + 2) { return exponent_end(s, stop, [80,112])^ }
    }
    return exponent_end(s, significand(s, start, false)^, [69,101])^
}

pub fn number_value(spelling: string) float? {
    // The lexer already admitted the spelling. PGF accepts finite underflow.
    let value = float(spelling) ^ { (+spelling) ^ { null } }
    return if (value is float and value != inf and value != -inf) value else null
}

fn failure(st, pos: int, text: string) map =>
    {pos:pos, count:st.count, value:null, problem:scan.problem(pos, text),
        prior:if (st.problem == null) st.prior else scan.add(st.prior, st.problem)}

fn success(st, pos: int, value) map => {pos:pos, count:st.count, value:value, problem:null}

fn power(b: int) int => if (b in [62,60,61,33]) 1 else if (b in [43,45]) 2
    else if (b in [42,47]) 3 else if (b == 94) 4 else 0

fn expression(s, initial, minimum: int, depth: int) map^ {
    if ((depth > 64 or initial.count >= 1024)^) {
        return failure(initial, initial.pos, "plot expression exceeds complexity limit")
    }
    var st = primary(s, {*:initial, count:initial.count + 1}, depth + 1)^
    while ((st.problem == null)^) {
        let pos = spaces(s, st.pos)^
        let b = scan.byte(s, pos)^
        if (b == 114 and not scan.ascii_alpha(scan.byte(s, pos + 1)^)) {
            st = success(st, pos + 1, <radian_value st.value>)
            continue
        }
        if (b == 63 and minimum == 0) {
            let yes = expression(s, scan.set_cursor(st, pos + 1), 0, depth + 1)^
            if (yes.problem != null) { return yes }
            let colon = spaces(s, yes.pos)^
            let colon_byte = scan.byte(s, colon)^
            if (colon_byte != 58) {
                return failure(yes, if (colon_byte == -1) colon else colon + 1,
                    "conditional plot expression requires ':'")
            }
            let no = expression(s, scan.set_cursor(yes, colon + 1), 0, depth + 1)^
            if (no.problem != null) { return no }
            st = success(no, no.pos, <conditional st.value yes.value no.value>)
            continue
        }
        let precedence = power(b)
        if (precedence == 0 or precedence < minimum) { return scan.set_cursor(st, pos) }
        let pair = precedence == 1 and scan.byte(s, pos + 1)^ == 61
        if (not pair and b in [61,33]) { return failure(st, pos, "unsupported plot comparison operator") }
        let next_pos = pos + (if (pair) 2 else 1)
        let rhs = expression(s, scan.set_cursor(st, next_pos), precedence + (if (b == 94) 0 else 1), depth + 1)^
        if (rhs.problem != null) { return rhs }
        let op = scan.text(s, pos, next_pos)^
        st = success(rhs, rhs.pos, <binary op:op, st.value rhs.value>)
    }
    return st
}

fn named(s, initial, pos: int, depth: int) map^ {
    var stop = pos
    while (true) {
        let b = scan.byte(s, stop)^
        if (not (scan.ascii_alpha(b) or scan.digit(b) or b == 95)) { break }
        stop = stop + 1
    }
    if (stop - pos >= 32) { return failure(initial, stop, "plot expression name too long") }
    let name = scan.text(s, pos, stop)^
    if (name in ["x","pi"]) { return success(initial, stop, <variable name:name>) }
    let group_at = spaces(s, stop)^
    if (scan.byte(s, group_at)^ != 40) { return success(initial, group_at, <variable name:name>) }
    var st = scan.set_cursor(initial, group_at + 1)
    var args = null
    while (true) {
        if (((args.count or 0) >= 8)^) { return failure(st, st.pos, "plot function exceeds eight arguments") }
        st = expression(s, st, 0, depth + 1)^
        if ((st.problem != null)^) { return st }
        args = scan.add(args, st.value)
        let next_pos = spaces(s, st.pos)^
        let b = scan.byte(s, next_pos)^
        if (b == -1) { return failure(st, next_pos, "unclosed plot function") }
        if (b == 41) {
            let children = scan.finish(args)^
            return success(st, next_pos + 1, <function_call name:name, *children>)
        }
        if (b != 44) { return failure(st, next_pos + 1, "plot function arguments require commas") }
        st = scan.set_cursor(st, next_pos + 1)
    }
}

fn primary(s, st, depth: int) map^ {
    let pos = spaces(s, st.pos)^
    let b = scan.byte(s, pos)^
    if (b == -1) { return failure(st, pos, "missing plot expression operand") }
    if (b in [43,45]) {
        let operand = expression(s, scan.set_cursor(st, pos + 1), 4, depth + 1)^
        if (operand.problem != null) { return operand }
        let op = scan.text(s, pos, pos + 1)^
        return success(operand, operand.pos, <unary op:op, operand.value>)
    }
    if (b == 40) {
        let outcome = expression(s, scan.set_cursor(st, pos + 1), 0, depth + 1)^
        let stop = spaces(s, outcome.pos)^
        let end_byte = scan.byte(s, stop)^
        if (end_byte != 41) {
            let next_pos = if (end_byte == -1) stop else stop + 1;
            return failure(outcome, next_pos, "unclosed plot expression group")
        }
        return scan.set_cursor(outcome, stop + 1)
    }
    if (scan.digit(b) or b == 46) {
        let stop = number_end(s, pos)^
        let value = if (stop == pos) null else number_value(scan.text(s, pos, stop)^)
        if (value == null) { return failure(st, pos, "invalid plot expression number") }
        return success(st, stop, <number_literal value:value>)
    }
    if (b == 92 and scan.ascii_alpha(scan.byte(s, pos + 1)^)) {
        var stop = pos + 2
        while (scan.ascii_alpha(scan.byte(s, stop)^)) { stop = stop + 1 }
        return success(st, stop, <variable name:scan.text(s, pos + 1, stop)^>)
    }
    if (b == 35 and scan.digit(scan.byte(s, pos + 1)^)) {
        return success(st, pos + 2, <variable name:scan.text(s, pos, pos + 2)^>)
    }
    if (scan.ascii_alpha(b)) { return named(s, st, pos, depth)^ }
    return failure(st, pos, "unsupported plot expression token")
}

pub fn read(bytes: binary?) map^ {
    let initial = {pos:0, count:0}
    let length = len(bytes)
    let parsed = if (length > 4096) failure(initial, 0, "plot expression exceeds 4096 bytes")
        else expression(scan.source(bytes)^, initial, 0, 0)^
    let problem = if (parsed.problem != null or parsed.pos == length) parsed.problem
        else scan.problem(parsed.pos, "unexpected plot expression token")
    let errors = if (problem == null) [] else [*scan.finish(parsed.prior)^, problem];
    {root:parsed.value, errors:errors}
}
