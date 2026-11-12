// Common mathematical functions use Lambda's native math functions and Excel errors.
import c: .mod_formula_common
pub fn pi() => math.pi
pub fn exp(value) any^ => math.exp(c.numeric(value)^)
pub fn ln(value) any^ { let n=c.numeric(value)^; if (n<=0) raise error("#NUM!") else math.log(n) }
pub fn log10(value) any^ { let n=c.numeric(value)^; if (n<=0) raise error("#NUM!") else math.log10(n) }
pub fn log(value,base=10) any^ {
    let n=c.numeric(value)^
    let b=c.numeric(base)^;
    if (n<=0 or b<=0 or b==1) raise error("#NUM!") else if (b==10) math.log10(n) else math.log(n)/math.log(b)
}
pub fn sin(value) any^ => math.sin(c.numeric(value)^)
pub fn cos(value) any^ => math.cos(c.numeric(value)^)
pub fn tan(value) any^ => math.tan(c.numeric(value)^)
pub fn asin(value) any^ { let n=c.numeric(value)^; if (abs(n)>1) raise error("#NUM!") else math.asin(n) }
pub fn acos(value) any^ { let n=c.numeric(value)^; if (abs(n)>1) raise error("#NUM!") else math.acos(n) }
pub fn atan(value) any^ => math.atan(c.numeric(value)^)
// Excel's ATAN2 takes x first; Lambda's math.atan2 takes y first.
pub fn atan2(x,y) any^ {
    let a=c.numeric(x)^
    let b=c.numeric(y)^;
    if (a==0 and b==0) raise error("#DIV/0!") else math.atan2(b,a)
}
pub fn degrees(value) any^ => c.numeric(value)^*180/math.pi
pub fn radians(value) any^ => c.numeric(value)^*math.pi/180
pub fn quotient(a,b) any^ {
    let x=c.numeric(a)^
    let y=c.numeric(b)^;
    if (y==0) raise error("#DIV/0!") else trunc(x/y)
}
pub fn even(value) any^ { let n=c.numeric(value)^; (if (n<0) -1 else 1)*ceil(abs(n)/2)*2 }
pub fn odd(value) any^ { let n=c.numeric(value)^; (if (n<0) -1 else 1)*(2*ceil((abs(n)-1)/2)+1) }
pub fn mround(value,multiple) any^ {
    let n=c.numeric(value)^
    let m=c.numeric(multiple)^;
    if (m==0) 0 else if (n*m<0) raise error("#NUM!") else (if (n<0) -1 else 1)*floor(abs(n/m)+0.5)*abs(m)
}
fn factorial_loop(n,index,acc) any^ => if (index>n) acc else factorial_loop(n,index+1,acc*index)^
pub fn fact(value) any^ {
    let n=trunc(c.numeric(value)^);
    if (n<0 or n>170) raise error("#NUM!") else factorial_loop(n,2,1.0)^
}
fn combinations(n,k,i,acc) any^ => if (acc is inf or acc is nan) raise error("#NUM!")
    else if (i>k) acc else combinations(n,k,i+1,acc*(n-k+i)/i)^
pub fn combin(total,chosen) any^ {
    let n=trunc(c.numeric(total)^)
    let k=trunc(c.numeric(chosen)^);
    if (n<0 or k<0 or k>n) raise error("#NUM!") else round(combinations(n,min(k,n-k),1,1.0)^)
}
pub fn sumsq(...) any^ { let values=c.numbers(varg())^; sum([for (n in values) n*n]) }
