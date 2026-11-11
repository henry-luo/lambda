// TeX dimensions are signed scaled points, not floating-point values.
fn decimal_accumulate(digits, index, accumulator) any^ =>
    if (index < 0) (accumulator + 1) div 2
    else decimal_accumulate(digits, index - 1, (accumulator + digits[index] * 131072) div 10)

pub fn round_decimal(digits) any^ => decimal_accumulate(digits, len(digits) - 1, 0)

pub fn unit(name) any^ =>
    if (name == "pt") [1, 1] else if (name == "in") [7227, 100]
    else if (name == "pc") [12, 1] else if (name == "cm") [7227, 254]
    else if (name == "mm") [7227, 2540] else if (name == "bp") [7227, 7200]
    else if (name == "dd") [1238, 1157] else if (name == "cc") [14856, 1157]
    else if (name == "nd") [685, 642] else if (name == "nc") [1370, 107]
    else null

fn scaled_digits(remainder, delta, output) any^ {
    let rounded = if (delta > 65536) remainder + 32768 - 50000 else remainder;
    let next = 10 * (rounded % 65536);
    let result = output ++ string(rounded div 65536);
    if (next <= delta * 10) result else scaled_digits(next, delta * 10, result)
}

pub fn format(value, unit = "pt") any^ {
    let n = abs(value);
    (if (value < 0) "-" else "") ++ string(n div 65536) ++ "." ++
        scaled_digits(10 * (n % 65536) + 5, 10, "") ++ unit
}
