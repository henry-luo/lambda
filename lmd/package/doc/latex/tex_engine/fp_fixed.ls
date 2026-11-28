// S1.8, S4.6.2, D10.2.2v2: fixed multiplication follows expl3's limb carries.
import reader: .reader
// expl3 takes the first five decimal tokens as a carry, even when a limb overflows.
fn pack(value: int) map^ {
    let text=string(value);
    {carry:int(reader.text_span(text,0,5))^,tail:int(reader.text_span(text,5,len(text)))^}
}
pub fn multiply(a, b) any^ {
    let a1=a[0]; let a2=a[1]; let a3=a[2]; let a4=a[3]; let a5=a[4]; let a6=a[5];
    let b1=b[0]; let b2=b[1]; let b3=b[2]; let b4=b[3]; let b5=b[4]; let b6=b[5];
    // TeX rounds its nonnegative integer division to the nearest integer.
    let tail=a3*b4+a4*b3+a5*b2+a6*b1+a1*b6+a2*b5;
    let d6=pack(500000000+a2*b4+a3*b3+a4*b2+(tail+5000) div 10000+a5*b1+a1*b5)^;
    let d5=pack(499950000+a1*b4+a2*b3+a3*b2+a4*b1+d6.carry)^;
    let d4=pack(499950000+a1*b3+a2*b2+a3*b1+d5.carry)^;
    let d3=pack(499950000+a1*b2+a2*b1+d4.carry)^;
    let d2=pack(499950000+a1*b1+d3.carry)^;
    [d2.carry-50000,d2.tail,d3.tail,d4.tail,d5.tail,d6.tail]
}

// Addition and ordered subtraction preserve the six-limb fixed value exactly.
fn scaled_value(limbs, index: int = 0, value = 0n) any^ =>
    if(index >= len(limbs)) value else scaled_value(limbs,index+1,value*10000n+limbs[index]);
fn split_value(value, remaining: int = 5, tail = []) any^ => if(remaining == 0)
    [int(value)^,*tail] else split_value(value div 10000,remaining-1,[int(value%10000)^,*tail]);
pub fn add(a,b) any^ => split_value(scaled_value(a)^ + scaled_value(b)^);
pub fn subtract(a,b) any^ {
    let left=scaled_value(a)^; let right=scaled_value(b)^;
    if(left < right) error("expl3-fixed-negative-subtraction") else split_value(left-right)
}

// The source's positive integer divisor rounds halfway cases upward.
pub fn divide_int(a, denominator: int) any^ {
    if(denominator <= 0) error("expl3-fixed-invalid-divisor")
    else split_value((scaled_value(a)^ + denominator div 2) div denominator)
}

// expl3's quotient estimates use the first five divisor digits and a -1 bias.
fn ln_division_loop(divisor, leading_divisor, remainder, index: int, quotient) any^ {
    let leading = remainder div 100000000n;
    let estimate = (leading + leading_divisor div 2) div leading_divisor;
    if (index == 5) quotient * 10000n + estimate
    else {
        let digit = estimate - 1;
        ln_division_loop(divisor, leading_divisor, remainder * 10000n - digit * divisor,
            index + 1, quotient * 10000n + digit)
    }
}
pub fn ln_ratio(divisor) any^ {
    let leading_divisor = divisor div 1000000000000n + 1;
    let quotient = ln_division_loop(divisor, leading_divisor, 20000000000000000n, 0, 0n)^;
    let one = 1000000000000000000000000n;
    let positive = quotient <= one;
    {positive:positive, limbs:split_value(if (positive) one - quotient else quotient - one)^}
}

// The conditioning multiplier selects the same range as expl3's first two digits.
pub fn ln_condition(significand) any^ {
    let first = significand div 1000000000000000n;
    let second = (significand div 100000000000000n) % 10;
    let multiplier = if (first == 1) (if (second < 4) 10 - second else 6)
        else if (first == 2) 4 else if (first == 3) 3
        else if (first <= 6) 2 else 1;
    {multiplier:multiplier, ratio:ln_ratio(10000000000000000n + multiplier * significand)^}
}

// The logarithm squares its six fractional limbs with its own carry sequence.
pub fn ln_square(t) any^ {
    let a=t[0]; let b=t[1]; let c=t[2]; let d=t[3]; let e=t[4]; let f=t[5];
    let tail=2*a*f+2*b*e+2*c*d;
    let final_limb=pack(100000000+2*a*e+2*b*d+c*c+(tail+5000) div 10000)^;
    let fourth=pack(99990000+2*a*d+2*b*c+final_limb.carry)^;
    let third=pack(99990000+2*a*c+b*b+fourth.carry)^;
    let second=pack(99990000+2*a*b+third.carry)^;
    let first=pack(99990000+a*a+second.carry)^;
    [first.carry-10000,first.tail,second.tail,third.tail,fourth.tail,final_limb.tail]
}

// expl3 evaluates the odd logarithm series in reverse order before doubling t.
fn ln_taylor_loop(square, denominator: int, accumulator) any^ {
    let one=[10000,0,0,0,0,0];
    if (denominator == 1) add(one,accumulator)
    else {
        let coefficient=divide_int(one,denominator)^;
        let next=multiply(add(coefficient,accumulator)^,square)^;
        ln_taylor_loop(square,denominator-2,next)
    }
}
pub fn ln_series(t) any^ {
    let square=ln_square(t)^;
    let series=ln_taylor_loop(square,21,[0,0,0,0,0,0])^;
    let twice=split_value(scaled_value(t)^ * 2)^;
    multiply(series,twice)
}

pub fn ln_significand(significand, constants) any^ {
    let conditioned=ln_condition(significand)^;
    let constant=[for (entry in constants where entry.multiplier == conditioned.multiplier) entry.limbs][0];
    let series=ln_series(conditioned.ratio.limbs)^;
    if (conditioned.ratio.positive) subtract(constant,series) else add(constant,series)
}

// Power rescales the signed exponent/log difference from 24 to 20 places.
pub fn power_scaled_log(logarithm, exponent: int, ln_ten) any^ {
    let difference=exponent * scaled_value(ln_ten)^ - scaled_value(logarithm)^;
    let magnitude=if (difference < 0) -difference else difference;
    split_value((magnitude + 5000) div 10000)
}

// The exponential uses a reverse Taylor recurrence with rounded limb division.
fn exp_taylor_loop(operand, denominator: int, accumulator) any^ {
    let one=[10000,0,0,0,0,0];
    if (denominator == 1) add(one,accumulator)
    else {
        let quotient=divide_int(accumulator,denominator)^;
        let next=multiply(add(one,quotient)^,operand)^;
        exp_taylor_loop(operand,denominator-1,next)
    }
}
pub fn exp_taylor(operand) any^ => exp_taylor_loop(operand,10,operand)

// Each nonzero decimal digit selects a precomputed exponential factor.
fn exp_factors_loop(digits, table, index: int, accumulator, exponent: int) any^ {
    if (index == 6) {limbs:accumulator, exponent:exponent}
    else if (digits[index] == 0) exp_factors_loop(digits,table,index+1,accumulator,exponent)
    else {
        let entry=[for (item in table where item.position == 5-index and item.digit == digits[index]) item][0];
        let product=multiply(entry.limbs,accumulator)^;
        exp_factors_loop(digits,table,index+1,product,exponent+entry.exponent)
    }
}
pub fn exp_factors(digits,table) any^ => exp_factors_loop(digits,table,0,[10000,0,0,0,0,0],0);

fn ten_power(exponent: int, value = 1n) any =>
    if (exponent == 0) value else ten_power(exponent-1,value*10n);
fn six_digits(value, remaining: int = 6, digits = []) any^ =>
    if (remaining == 0) digits else six_digits(value div 10,remaining-1,[int(value % 10)^,*digits]);
// pow_B shifts its fixed-point product by four extra decimal places.
pub fn power_exp_parts(product, exponent: int, table) any^ {
    let shift=exponent+4;
    let raw=scaled_value(product)^;
    let value=if (shift >= 0) raw * ten_power(shift) else raw div ten_power(-shift);
    let unit=100000000000000000000000n;
    // The digit table covers five integer digits and one fractional digit.
    if (value div unit >= 1000000n) null
    else {
        let digits=six_digits(value div unit)^;
        let remainder=split_value(value % unit)^;
        let factor=exp_factors(digits,table)^;
        let fraction=exp_taylor(remainder)^;
        {limbs:multiply(fraction,factor.limbs)^, exponent:factor.exponent}
    }
}

// Reciprocal estimation multiplies a full fixed value by a three-limb fraction.
pub fn multiply_short(a,b) any^ {
    let a1=a[0]; let a2=a[1]; let a3=a[2]; let a4=a[3]; let a5=a[4]; let a6=a[5];
    let b1=b[0]; let b2=b[1]; let b3=b[2];
    let tail=a5*b3+a6*b2+(a6*b3+5000) div 10000;
    let d6=pack(500000000+a4*b3+a5*b2+a6*b1+(tail+5000) div 10000)^;
    let d5=pack(499950000+a3*b3+a4*b2+a5*b1+d6.carry)^;
    let d4=pack(499950000+a2*b3+a3*b2+a4*b1+d5.carry)^;
    let d3=pack(499950000+a1*b3+a2*b2+a3*b1+d4.carry)^;
    let d2=pack(499950000+a1*b2+a2*b1+d3.carry)^;
    [a1*b1+d2.carry-50000,d2.tail,d3.tail,d4.tail,d5.tail,d6.tail]
}

// Follow expl3's reciprocal estimate and error correction at its limb precision.
pub fn reciprocal_normalized(divisor) any^ {
    let first=divisor[0]; let second=divisor[1];
    let estimate1=(1000000000+(first+1) div 2) div (first+1);
    let estimate2=(1000000000+first div 2) div first;
    let estimate=1000000000-1750+estimate1*1000+
        (estimate2-estimate1)*(1000-(second+5) div 10);
    let digits=estimate-1000000000;
    let short=[digits div 100000000,(digits div 10000)%10000,digits%10000];
    let initial=[digits div 100000,(digits div 10)%10000,(digits%10)*1000,0,0,0];
    let one=[10000,0,0,0,0,0];
    let epsilon=subtract(one,multiply_short(divisor,short)^)^;
    let square=multiply(epsilon,epsilon)^;
    let factor=multiply(add(one,square)^,add(one,epsilon)^)^;
    let correction_short=multiply_short(epsilon,[initial[0],initial[1],initial[2]])^;
    let correction_scaled=[0,correction_short[0],correction_short[1],correction_short[2],correction_short[3],correction_short[4]];
    let correction=multiply(correction_scaled,factor)^;
    let reciprocal=add(initial,correction)^;
    multiply(reciprocal,[1000,0,0,0,0,0])
}

// TeX's fixed-to-float conversion rounds a discarded 5 upward.
pub fn float_from_fixed(limbs, exponent: int) any^ {
    let raw=scaled_value(limbs)^;
    if (raw == 0) error("expl3-fixed-zero-conversion")
    else {
        let raw_places=len(string(raw));
        let divisor_power=ten_power(abs(raw_places-16));
        let rounded=if (raw_places >= 16) (raw+divisor_power div 2) div divisor_power else raw*divisor_power;
        let overflow=rounded >= 10000000000000000n;
        {digits:string(if (overflow) rounded div 10 else rounded), exponent:exponent+(raw_places-24)+(if (overflow) 1 else 0)}
    }
}
// Normalize the six limbs before the reciprocal estimate and correction.
pub fn reciprocal_float(limbs) any^ {
    let value=scaled_value(limbs)^;
    if (value == 0) error("expl3-fixed-zero-reciprocal")
    else {
        let places=len(string(value));
        let adjustment=24-places;
        let normalized=if (adjustment >= 0) value*ten_power(adjustment) else value div ten_power(-adjustment);
        let result=reciprocal_normalized(split_value(normalized)^)^;
        float_from_fixed(result,2-(places-24))
    }
}
