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

// The mode reverses rounding for negative numbers only.
fn multiple_round(value,significance,mode,up) any^ {
    let n=c.numeric(value)^
    let m=abs(c.numeric(significance)^)
    let reverse=c.numeric(mode)^!=0;
    if (m==0) 0 else {
        let toward_up=if (n<0 and reverse) not up else up;
        (if (toward_up) ceil(n/m) else floor(n/m))*m
    }
}
pub fn ceiling_math(value,significance=1,mode=0) any^ => multiple_round(value,significance,mode,true)^
pub fn floor_math(value,significance=1,mode=0) any^ => multiple_round(value,significance,mode,false)^
pub fn ceiling_precise(value,significance=1) any^ => multiple_round(value,significance,0,true)^
pub fn floor_precise(value,significance=1) any^ => multiple_round(value,significance,0,false)^
pub fn sqrtpi(value) any^ { let n=c.numeric(value)^; if (n<0) raise error("#NUM!") else sqrt(n*math.pi) }
pub fn sinh(value) any^ { let n=c.numeric(value)^; (math.exp(n)-math.exp(-n))/2 }
pub fn cosh(value) any^ { let n=c.numeric(value)^; (math.exp(n)+math.exp(-n))/2 }
pub fn tanh(value) any^ { let n=c.numeric(value)^; if (n>350) 1 else if (n< -350) -1 else { let x=math.exp(2*n); (x-1)/(x+1) } }
fn gcd_pair(a,b) any^ => if (b==0) a else gcd_pair(b,a%b)^
fn integer_values(args) any^ {
    let values=c.numbers(args)^;
    if (any([for (v in values) v<0 or v>9007199254740991])) raise error("#NUM!") else [for (v in values) trunc(v)]
}
fn gcd_loop(values,i,result) any^ => if (i>=len(values)) result else gcd_loop(values,i+1,gcd_pair(result,values[i])^)^
fn lcm_loop(values,i,result) any^ {
    if (i>=len(values)) result else if (result==0 or values[i]==0) lcm_loop(values,i+1,0)^
    else { let next=result/gcd_pair(result,values[i])^*values[i]; if (next>9007199254740991) raise error("#NUM!") else lcm_loop(values,i+1,next)^ }
}
pub fn gcd(...) any^ { let v=integer_values(varg())^; if (len(v)==0) raise error("#VALUE!") else gcd_loop(v,1,v[0])^ }
pub fn lcm(...) any^ { let v=integer_values(varg())^; if (len(v)==0) raise error("#VALUE!") else lcm_loop(v,1,v[0])^ }
