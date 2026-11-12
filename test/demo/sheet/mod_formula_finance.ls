// Scalar time-value functions follow the standard Excel cash-flow sign convention.
import c: .mod_formula_common
fn checked(rate,periods,kind) any^ {
    let r=c.numeric(rate)^
    let n=c.numeric(periods)^
    let t=c.numeric(kind)^;
    if (r<= -1 or n<0 or not contains([0,1],t)) raise error("#NUM!") else {rate:r,periods:n,kind:t}
}
fn annuity(r,n) => if (r==0) n else ((1+r)**n-1)/r
pub fn fv(rate,periods,payment,present=0,kind=0) any^ {
    let p=checked(rate,periods,kind)^
    let pay=c.numeric(payment)^
    let pv=c.numeric(present)^;
    -(pv*(1+p.rate)**p.periods+pay*(1+p.rate*p.kind)*annuity(p.rate,p.periods))
}
pub fn pv(rate,periods,payment,future=0,kind=0) any^ {
    let p=checked(rate,periods,kind)^
    let pay=c.numeric(payment)^
    let future_value=c.numeric(future)^
    let growth=(1+p.rate)**p.periods;
    if (growth==0) raise error("#DIV/0!") else -(future_value+pay*(1+p.rate*p.kind)*annuity(p.rate,p.periods))/growth
}
pub fn pmt(rate,periods,present,future=0,kind=0) any^ {
    let p=checked(rate,periods,kind)^
    let present_value=c.numeric(present)^
    let future_value=c.numeric(future)^
    let factor=(1+p.rate*p.kind)*annuity(p.rate,p.periods);
    if (factor==0) raise error("#DIV/0!") else -(present_value*(1+p.rate)**p.periods+future_value)/factor
}
pub fn nper(rate,payment,present,future=0,kind=0) any^ {
    let r=c.numeric(rate)^
    let pay=c.numeric(payment)^
    let present_value=c.numeric(present)^
    let future_value=c.numeric(future)^
    let t=c.numeric(kind)^;
    if (r<= -1 or not contains([0,1],t)) raise error("#NUM!")
    else if (r==0) (if (pay==0) raise error("#DIV/0!") else -(present_value+future_value)/pay)
    else {
        let payment_factor=pay*(1+r*t)
        let denominator=payment_factor+present_value*r
        let numerator=payment_factor-future_value*r;
        if (denominator==0 or numerator/denominator<=0) raise error("#NUM!") else math.log(numerator/denominator)/math.log(1+r)
    }
}
pub fn npv(rate,...) any^ {
    let r=c.numeric(if (rate is map and rate.kind=="cellref") rate.value else rate)^
    let values=c.numbers(varg())^;
    if (r<= -1) raise error("#NUM!") else sum([for (i,value in values) value/((1+r)**(i+1))])
}

pub fn sln(cost,salvage,life) any^ {
    let base=c.numeric(cost)^-c.numeric(salvage)^
    let n=c.numeric(life)^;
    if (n==0) raise error("#DIV/0!") else base/n
}
pub fn syd(cost,salvage,life,period) any^ {
    let base=c.numeric(cost)^-c.numeric(salvage)^
    let n=c.numeric(life)^
    let p=c.numeric(period)^;
    if (n<=0 or p<1 or p>n) raise error("#NUM!") else base*(n-p+1)*2/(n*(n+1))
}
