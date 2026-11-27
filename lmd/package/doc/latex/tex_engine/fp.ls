// decimal operations for the script TeX floating-point expression evaluator.
// a finite value is rounded to the expl3 precision after each operation.
import reader: .reader
import fixed: .fp_fixed
import pow_constants: .fp_constants

type DecimalParts = {negative: bool, digits: string, point: int, exponent: int}
type ParseStep = {issue: string?, index: int, value?: decimal, unsupported?: string}
pub type Evaluation = {issue: string?, value?: string, index?: int, unsupported?: string}
fn ch(s: string, i: int) string => if (i < 0 or i >= len(s)) "" else reader.text_span(s, i, i + 1) or ""
fn digit(c: string) bool => c != "" and index_of("0123456789", c) != null
fn skip(s: string, i: int) int => if (ch(s, i) == " ") skip(s, i + 1) else i
fn zeroes(count: int, out: string = "") string => if (count <= 0) out else zeroes(count - 1, out ++ "0")
fn first_nonzero(s: string, i: int = 0) int => if (i >= len(s) or ch(s, i) != "0") i else first_nonzero(s, i + 1)
fn trim_zeros(s: string) string => if (len(s) > 0 and ch(s, len(s) - 1) == "0") trim_zeros(reader.text_span(s, 0, len(s) - 1)) else s

fn parts(value: decimal) DecimalParts^ {
    let raw = string(value);
    let negative = starts_with(raw, "-");
    let unsigned = if (negative) reader.text_span(raw, 1, len(raw)) else raw;
    let e = index_of(upper(unsigned), "E");
    let mantissa = if (e == null) unsigned else reader.text_span(unsigned, 0, e);
    let exponent = if (e == null) 0 else int(reader.text_span(unsigned, e + 1, len(unsigned))) ^ { 0 };
    let dot = index_of(mantissa, ".");
    {negative: negative, digits: replace(mantissa, ".", "") or "",
        point: if (dot == null) len(mantissa) else dot, exponent: exponent}
}

fn magnitude(value: decimal) int^ {
    let p = parts(value)^;
    let first = first_nonzero(p.digits);
    if (first == len(p.digits)) 0 else p.exponent + p.point - first - 1
}

// decimal ties are rounded to an even last digit, independently of Lambda's round().
fn nearest_even(value: decimal, tie: decimal = 0m) decimal^ {
    let whole = trunc(value)^;
    let difference = abs(value - whole)^;
    // Nonzero tie modes choose positive or negative infinity, as expl3 does.
    let tie_up = if (tie > 0m) value > 0m else if (tie < 0m) value < 0m else abs(whole % 2m)^ == 1m;
    let up = difference > 0.5m or (difference == 0.5m and tie_up);
    if (not up) whole else whole + if (value < 0m) -1m else 1m
}

fn places(value: decimal, digits: int, tie: decimal = 0m) decimal^ {
    let power = 10m ** abs(digits);
    if (digits >= 0) nearest_even(value * power,tie)^ / power
    else nearest_even(value / power,tie)^ * power
}

// Scale decimal places before directed rounding and keep a negative zero result.
fn directed_places(value: decimal, digits: int, name: string) decimal^ {
    if (value == 0m) value
    else {
        let power = 10m ** abs(digits);
        let scaled = if (digits >= 0) value * power else value / power;
        let whole = if (name == "trunc") trunc(scaled)^ else if (name == "floor") floor(scaled)^ else ceil(scaled)^;
        let result = if (digits >= 0) whole / power else whole * power;
        if (result == 0m) value * 0m else result
    }
}

fn normalize(value: decimal) decimal^ {
    if (value == 0m) value
    else {
        let exp = magnitude(value)^;
        if (exp < -200 or exp > 200) error("expl3-fp-range")
        else places(value, 15 - exp)
    }
}

fn square_root_step(x: decimal, current: decimal, previous: decimal, remaining: int) decimal^ {
    if (remaining <= 0) current
    else {
        let next = (current + x / current) / 2m;
        if (next == current or next == previous) next
        else square_root_step(x, next, current, remaining - 1)
    }
}

fn square_root(value: decimal) decimal^ {
    if (value < 0m) error("expl3-fp-domain")
    else if (value == 0m) value
    else {
        let exp = magnitude(value)^;
        let half = int(floor(exp / 2))^;
        let start = 10m ** half;
        normalize(square_root_step(value, start, 0m, 20)^)
    }
}

// Reduce bounded angles with enough decimal precision for expl3's 16 digits.
fn trig_series(x: decimal, cosine: bool, term: decimal, sum: decimal, n: int) decimal^ {
    let a = if (cosine) 2 * n - 1 else 2 * n;
    let b = a + 1;
    let next = -term * x * x / decimal(a * b)^;
    let total = sum + next;
    if (n >= 40 or total == sum) total else trig_series(x,cosine,next,total,n + 1)
}
// reduce scaled arguments with exact integers; the constant is floor(pi/2 * 10^240).
// verify with utils/generate_latex_trig_constant.py --check.
let QUARTER_SCALED = 1570796326794896619231321691639751442098584699687552910487472296153908203143104499314017412671058533991074043256641153323546922304775291115862679704064240558725142051350969260552779822311474477465190982214405487832966723064237824116893391582n;
fn reduction_digit(value, count: int = 0) any^ => if(value < QUARTER_SCALED)
    {remainder:value,digit:count} else reduction_digit(value - QUARTER_SCALED,count + 1);
fn reduction_scan(digits: string, zeros: int, index: int = 0, remainder = 0n, quadrant: int = 0, has_quotient: bool = false) any^ {
    if(index >= len(digits) + zeros) {remainder:remainder,quadrant:quadrant,has_quotient:has_quotient}
    else {
        let next = if(index < len(digits)) int(ch(digits,index))^ else 0;
        let step = reduction_digit(remainder * 10n + next)^;
        reduction_scan(digits,zeros,index + 1,step.remainder,(quadrant * 10 + step.digit) % 4,has_quotient or step.digit != 0)
    }
}
fn reduce_argument(value: decimal) any^ {
    let p = parts(abs(value)^)^;
    let zeros = 240 + p.point + p.exponent - len(p.digits);
    if(zeros < 0) {issue:"expl3-fp-unsupported"}
    else {
        let divided = reduction_scan(p.digits,zeros)^;
        let upward = divided.remainder * 2n > QUARTER_SCALED or
            (divided.remainder * 2n == QUARTER_SCALED and divided.quadrant % 2 == 1);
        let remainder = if(upward) divided.remainder - QUARTER_SCALED else divided.remainder;
        let quadrant = (divided.quadrant + if(upward) 1 else 0) % 4;
        let signed = if(value < 0m) -remainder else remainder;
        let angle = decimal(string(signed))^ / 1e240m;
        // no pi error is introduced when the selected quotient is zero.
        let pi_error = if(not divided.has_quotient and not upward) 0m else (abs(value)^ + 1m) * 1e-240m;
        {issue:null,angle:angle,
            quadrant:if(value < 0m) (4 - quadrant) % 4 else quadrant,
            uncertainty:pi_error + abs(angle)^ * 1e-198m}
    }
}
// reject quadrant and 16-digit rounding decisions that cross their error intervals.
fn trig_reduced(angle: decimal, quadrant: int, reduction_error: decimal, cosine: bool) any^ {
    // the extra margin keeps the rounded boundary below the true pi/4.
    let quarter_bound = decimal(string(QUARTER_SCALED))^ / 2e240m - 1e-198m;
    if(abs(angle)^ + reduction_error >= quarter_bound) {issue:"expl3-fp-unsupported"}
    else {
        let use_cos = if (cosine) quadrant == 0 or quadrant == 2 else quadrant == 1 or quadrant == 3;
        let negative = if (cosine) quadrant == 1 or quadrant == 2 else quadrant == 2 or quadrant == 3;
        let first = if(use_cos) 1m else angle;
        let raw = trig_series(angle,use_cos,first,first,1)^;
        let result = if(negative) -raw else raw;
        let uncertainty = reduction_error + abs(result)^ * 1e-32m;
        let low = normalize(result - uncertainty) ^ { null };
        let high = normalize(result + uncertainty) ^ { null };
        if(low == null or high == null or low != high) {issue:"expl3-fp-unsupported"}
        else {issue:null,value:normalize(result)^,raw:result,uncertainty:uncertainty}
    }
}
fn trig(value: decimal, cosine: bool) any^ {
    if(value == 0m and not cosine) {issue:null,value:value,raw:value,uncertainty:0m}
    else {
        let quarter_hi = 1.570796326794897m;
        let quarter_lo = -0.0000000000000003807686783083602485579014153003124m;
        let turns = nearest_even(value / (quarter_hi + quarter_lo))^;
        let angle = (value - turns * quarter_hi) - turns * quarter_lo;
        let reduction_error = abs(turns)^ * 1e-49m;
        let quadrant = int((turns % 4m + 4m) % 4m)^;
        let fast = trig_reduced(angle,quadrant,reduction_error,cosine)^;
        if(fast.issue == null) fast
        else {
            let exact = reduce_argument(value)^;
            if(exact.issue != null) exact else trig_reduced(exact.angle,exact.quadrant,exact.uncertainty,cosine)
        }
    }
}

// Propagate both trig intervals through the quotient before accepting a result.
fn tangent(value: decimal) any^ {
    if (value == 0m) {issue:null,value:value}
    else {
        let numerator = trig(value,false)^;
        let denominator = trig(value,true)^;
        if (numerator.issue != null or denominator.issue != null or
            abs(denominator.raw)^ <= denominator.uncertainty) {issue:"expl3-fp-unsupported"}
        else {
            let ratio = numerator.raw / denominator.raw;
            let uncertainty = (numerator.uncertainty + abs(ratio)^ * denominator.uncertainty) /
                (abs(denominator.raw)^ - denominator.uncertainty) + abs(ratio)^ * 1e-32m;
            let low = normalize(ratio - uncertainty) ^ { null };
            let high = normalize(ratio + uncertainty) ^ { null };
            if (low == null or high == null or low != high) {issue:"expl3-fp-unsupported"}
            else {issue:null,value:normalize(ratio)^}
        }
    }
}

fn render(value: decimal) string^ {
    let p = parts(value)^;
    let first = first_nonzero(p.digits);
    if (first == len(p.digits)) if (p.negative) "-0" else "0"
    else {
        let digits = trim_zeros(reader.text_span(p.digits, first, len(p.digits)));
        let point = p.point + p.exponent - first;
        let body = if (point <= 0) "0." ++ zeroes(-point) ++ digits
            else if (point >= len(digits)) digits ++ zeroes(point - len(digits))
            else reader.text_span(digits, 0, point) ++ "." ++ reader.text_span(digits, point, len(digits));
        (if (p.negative) "-" else "") ++ body
    }
}

fn span_digits(source: string, index: int) int => if (digit(ch(source, index))) span_digits(source, index + 1) else index
fn read_number(source: string, start: int) ParseStep^ {
    let integer_end = span_digits(source, start);
    let fraction_end = if (ch(source, integer_end) == ".") span_digits(source, integer_end + 1) else integer_end;
    let digits_present = integer_end > start or fraction_end > integer_end + 1;
    let e = ch(source, fraction_end);
    let has_exp = e == "e" or e == "E";
    let exp_start = fraction_end + if (has_exp) 1 else 0;
    let signed = ch(source, exp_start) == "+" or ch(source, exp_start) == "-";
    let exp_digits = exp_start + if (signed) 1 else 0;
    let end = if (has_exp) span_digits(source, exp_digits) else fraction_end;
    if (not digits_present or (has_exp and end == exp_digits)) {issue: "expl3-fp-number", index: start}
    else if ((integer_end - start) + (if (fraction_end > integer_end) fraction_end - integer_end - 1 else 0) > 30)
        {issue: "expl3-fp-range", index: start}
    else if (has_exp and (end - exp_digits > 3 or abs(int(reader.text_span(source, exp_start, end)) ^ { 999 }) > 200))
        {issue: "expl3-fp-range", index: start}
    else {
        // Numeric input errors become parser diagnostics before arithmetic sees a value.
        let value = decimal(reader.text_span(source, start, end)) ^ {
            return {issue: "expl3-fp-number", index: start}
        };
        let rounded = normalize(value) ^ {
            return {issue: "expl3-fp-range", index: start}
        };
        {issue: null, value: rounded, index: end}
    }
}

fn ident_end(source: string, index: int) int {
    let c = ch(source, index);
    if (c != "" and ((c >= "a" and c <= "z") or (c >= "A" and c <= "Z"))) ident_end(source, index + 1) else index
}

fn primary(source: string, index: int) ParseStep^ {
    let at = skip(source, index);
    let c = ch(source, at);
    if (c == "(") {
        let nested = expression(source, at + 1)^;
        let end = skip(source, nested.index);
        if (nested.issue != null) nested
        else if (ch(source, end) != ")") {issue: "expl3-fp-missing-paren", index: end}
        else {*:nested, index: end + 1}
    }
    else if (digit(c) or c == ".") read_number(source, at)
    else {
        let end = ident_end(source, at);
        let name = reader.text_span(source, at, end);
        let opening = skip(source, end);
        if (name == "" or ch(source, opening) != "(") {issue: "expl3-fp-unsupported", unsupported: name, index: at}
        else call(source, name, opening + 1)
    }
}

// Equal operands keep the first value, including the sign of zero.
fn extreme_tail(source: string, name: string, current: ParseStep) ParseStep^ {
    let at = skip(source, current.index);
    if (current.issue != null) current
    else if (ch(source, at) == ")") {*:current, index: at + 1}
    else if (ch(source, at) != ",") {issue: "expl3-fp-missing-paren", index: at}
    else {
        let next = expression(source, at + 1)^;
        if (next.issue != null) next
        else {
            let choose_next = if (name == "min") next.value < current.value else next.value > current.value;
            extreme_tail(source, name, {*:next, value: if (choose_next) next.value else current.value})
        }
    }
}

fn call(source: string, name: string, start: int) ParseStep^ {
    let first = expression(source, start)^;
    let at = skip(source, first.index);
    if (first.issue != null) first
    else if (name == "min" or name == "max") extreme_tail(source, name, first)
    else {
        let second = if (ch(source, at) == ",") expression(source, at + 1)^ else null;
        let after_second = if (second == null) at else skip(source, second.index);
        let third = if (second != null and ch(source,after_second) == ",") expression(source,after_second + 1)^ else null;
        let end = if (third == null) after_second else skip(source,third.index);
        if (second != null and second.issue != null) second
        else if (third != null and third.issue != null) third
        else if (ch(source, end) != ")") {issue: "expl3-fp-missing-paren", index: end}
        else if (name == "sqrt" and second == null) {
            let value = square_root(first.value) ^ { null };
            if (value == null) {issue: "expl3-fp-domain", index: end}
            else {issue: null, value: value, index: end + 1}
        }
        else if (contains(["sin","cos"],name) and second == null) {
            let value = trig(first.value,name == "cos")^;
            if(value.issue != null) {issue:value.issue,unsupported:name,index:end}
            else {issue:null,value:value.value,index:end + 1}
        }
        else if (name == "tan" and second == null) {
            let value = tangent(first.value)^;
            if(value.issue != null) {issue:value.issue,unsupported:name,index:end}
            else {issue:null,value:value.value,index:end + 1}
        }
        else if (name == "abs" and second == null)
            {issue: null, value: abs(first.value), index: end + 1}
        else if (name == "sign" and second == null)
            {issue: null, value: if (first.value == 0m) first.value else if (first.value < 0m) -1m else 1m, index: end + 1}
        else if (contains(["trunc","floor","ceil"],name) and third == null and
            (second == null or (second.value >= -200m and second.value <= 200m and second.value == trunc(second.value)^))) {
            let digits = if (second == null) 0 else int(second.value)^;
            let value = normalize(directed_places(first.value,digits,name)^) ^ { null };
            if (value == null) {issue: "expl3-fp-range", index: end}
            else {issue: null, value: value, index: end + 1}
        }
        else if (name == "round" and (second == null or (second.value >= -200m and second.value <= 200m and second.value == trunc(second.value)^))) {
            let digits = if (second == null) 0 else int(second.value)^;
            let tie = if (third == null) 0m else third.value;
            let value = normalize(places(first.value, digits, tie)^) ^ { null };
            if (value == null) {issue: "expl3-fp-range", index: end}
            else {issue: null, value: value, index: end + 1}
        }
        else {issue: "expl3-fp-unsupported", unsupported: name, index: start}
    }
}

fn unary(source: string, index: int) ParseStep^ {
    let at = skip(source, index);
    let c = ch(source, at);
    if (c == "+") unary(source, at + 1)
    else if (c == "-") {
        let result = unary(source, at + 1)^;
        if (result.issue != null) result else {*:result, value: -1m * result.value}
    }
    else power(source, at)
}

// Native expl3 uses its own fixed-point logarithm and exponential for power.
fn power_components(value: decimal) any^ {
    let p=parts(abs(value)^)^;
    let first=first_nonzero(p.digits);
    let digits=reader.text_span(p.digits,first,len(p.digits)) ++ zeroes(16);
    let a=int(reader.text_span(digits,0,4))^;
    let b=int(reader.text_span(digits,4,8))^;
    let c=int(reader.text_span(digits,8,12))^;
    let d=int(reader.text_span(digits,12,16))^;
    {significand:((a*10000n+b)*10000n+c)*10000n+d,
        limbs:[a,b,c,d,0,0],exponent:p.exponent+p.point-first}
}
fn power_value(base: decimal, exponent: decimal) any^ {
    if (exponent == 0m) {issue:null,value:1m}
    else if (base == 0m) {
        let negative_zero=parts(base)^.negative and exponent == trunc(exponent)^ and abs(exponent % 2m)^ == 1m;
        {issue:null,value:if (negative_zero) base else 0m}
    }
    else {
        let positive=abs(base)^;
        let negative=base < 0m and abs(exponent % 2m)^ == 1m;
        if (positive == 1m) {issue:null,value:if (negative) -1m else 1m}
        else {
            let x=power_components(positive)^;
            let y=power_components(exponent)^;
            if (y.exponent >= 22) {issue:"expl3-fp-unsupported",unsupported:"power range"}
            else {
                let logarithm=fixed.ln_significand(x.significand,pow_constants.logarithms)^;
                let ten=[for (entry in pow_constants.logarithms where entry.multiplier == 10) entry.limbs][0];
                let scaled=fixed.power_scaled_log(logarithm,x.exponent,ten)^;
                let product=fixed.multiply(scaled,y.limbs)^;
                let parts=fixed.power_exp_parts(product,y.exponent,pow_constants.exponentials) ^ { null };
                if (parts == null) {issue:"expl3-fp-unsupported",unsupported:"power range"}
                else {
                    let inverse=(positive < 1m) != (exponent < 0m);
                    let fp=if (inverse) fixed.reciprocal_float(parts.limbs)^ else fixed.float_from_fixed(parts.limbs,0)^;
                    let result_exp=fp.exponent+(if (inverse) -parts.exponent else parts.exponent);
                    if (result_exp < -199 or result_exp > 201) {issue:"expl3-fp-unsupported",unsupported:"power range"}
                    else {
                        let value=decimal(fp.digits)^ * (10m ** (result_exp-16));
                        {issue:null,value:if (negative) -value else value}
                    }
                }
            }
        }
    }
}

// Power binds more tightly than unary signs and associates to the right.
fn power(source: string, index: int) ParseStep^ {
    let base = primary(source,index)^;
    let at = skip(source,base.index);
    if(base.issue != null or ch(source,at) != "^") base
    else {
        let exponent = unary(source,at + 1)^;
        if(exponent.issue != null) exponent
        // Domain diagnostics and non-finite results still require TeX handling.
        else if((base.value < 0m and exponent.value != trunc(exponent.value)^) or
            (base.value == 0m and exponent.value < 0m))
            {issue:"expl3-fp-unsupported",unsupported:"power domain",index:at}
        else {
            let result=power_value(base.value,exponent.value)^;
            if (result.issue != null) {*:result,index:at}
            else {issue:null,value:result.value,index:exponent.index}
        }
    }
}

fn term_tail(source: string, left: ParseStep) ParseStep^ {
    let at = skip(source, left.index);
    let operator = ch(source, at);
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
fn term(source: string, index: int) ParseStep^ => term_tail(source, unary(source, index)^)
fn expression_tail(source: string, left: ParseStep) ParseStep^ {
    let at = skip(source, left.index);
    let operator = ch(source, at);
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
fn expression(source: string, index: int) ParseStep^ => expression_tail(source, term(source, index)^)

pub fn evaluate(source: string) Evaluation^ {
    let result = expression(source, 0)^;
    let end = skip(source, result.index);
    if (result.issue != null) result
    else if (end != len(source)) {issue: "expl3-fp-unsupported", unsupported: ch(source, end), index: end}
    else {issue: null, value: render(result.value)^}
}
