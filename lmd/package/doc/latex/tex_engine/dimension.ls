// TeX dimensions are signed scaled points, not floating-point values.
// Fraction digits are scanned integers; decimal arithmetic has fixed positive divisors.
pub fn round_decimal(digits: int[]) int {
    var at = len(digits) - 1, accumulator = 0
    while (at >= 0) {
        accumulator = (accumulator + digits[at] * 131072) div 10;
        at = at - 1
    }
    return (accumulator + 1) div 2
}


pub fn unit(name: string) int[]? =>
    if (name == "pt") [1, 1] else if (name == "in") [7227, 100]
    else if (name == "pc") [12, 1] else if (name == "cm") [7227, 254]
    else if (name == "mm") [7227, 2540] else if (name == "bp") [7227, 7200]
    else if (name == "dd") [1238, 1157] else if (name == "cc") [14856, 1157]
    else if (name == "nd") [685, 642] else if (name == "nc") [1370, 107]
    else null

fn scaled_digits(remainder: int, delta: int, output: string) string {
    let rounded = if (delta > 65536) remainder + 32768 - 50000 else remainder;
    let next = 10 * (rounded % 65536);
    let result = output ++ string(rounded div 65536);
    if (next <= delta * 10) result else scaled_digits(next, delta * 10, result)
}

pub fn format(value: int, unit: string = "pt") string {
    let n = abs(value);
    (if (value < 0) "-" else "") ++ string(n div 65536) ++ "." ++
        scaled_digits(10 * (n % 65536) + 5, 10, "") ++ unit
}
