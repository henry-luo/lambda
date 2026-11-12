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

// Cash-flow roots use Newton iteration with an explicit convergence bound.
fn cash_present(rate,values,exponents) => sum([for (i,value in values) value/(1+rate)**exponents[i]])
fn cash_derivative(rate,values,exponents) => sum([for (i,value in values) -exponents[i]*value/(1+rate)**(exponents[i]+1)])
fn root_cash(values,exponents,rate,iteration=0) any^ {
    if (rate<= -1 or iteration>=100) raise error("#NUM!") else {
        let result=cash_present(rate,values,exponents)
        let derivative=cash_derivative(rate,values,exponents);
        if (abs(result)<0.00000001) rate
        else if (derivative==0) raise error("#NUM!")
        else {
            let next=rate-result/derivative;
            if (next is nan or next is inf or next == -inf or next<= -1) raise error("#NUM!")
            else if (abs(next-rate)<0.0000001) next else root_cash(values,exponents,next,iteration+1)^
        }
    }
}
fn cash_flows(input) any^ {
    let values=[for (value in c.range_values(input) where value is number) value];
    if (len(values)<2 or not any([for (v in values) v<0]) or not any([for (v in values) v>0])) raise error("#NUM!") else values
}
pub fn irr(input,guess=0.1) any^ {
    let values=cash_flows(input)^
    let rate=c.numeric(guess)^;
    root_cash(values,[for (i,value in values) i],rate)^
}
fn dated_cash(input,dates) any^ {
    let raw_values=c.range_values(input)
    let raw_dates=c.range_values(dates);
    if (len(raw_values)!=len(raw_dates) or len(raw_values)==0 or
        not all([for (v in raw_values) v is number]) or not all([for (d in raw_dates) d is number])) raise error("#VALUE!")
    else {
        let initial=floor(raw_dates[0]);
        if (initial<0 or any([for (d in raw_dates) floor(d)<initial])) raise error("#NUM!")
        else {values:raw_values,exponents:[for (d in raw_dates) (floor(d)-initial)/365.0]}
    }
}
pub fn xnpv(rate,input,dates) any^ {
    let r=c.numeric(rate)^
    let cash=dated_cash(input,dates)^;
    if (r<= -1) raise error("#NUM!") else cash_present(r,cash.values,cash.exponents)
}
pub fn xirr(input,dates,guess=0.1) any^ {
    let cash=dated_cash(input,dates)^
    let values=cash_flows(cash.values)^;
    root_cash(values,cash.exponents,c.numeric(guess)^)^
}

fn rate_equation(rate,periods,payment,present,future,kind) =>
    present*(1+rate)**periods+payment*(1+rate*kind)*annuity(rate,periods)+future
fn rate_root(periods,payment,present,future,kind,guess,iteration=0) any^ {
    if (guess<= -1 or iteration>=100) raise error("#NUM!") else {
        let value=rate_equation(guess,periods,payment,present,future,kind)
        let step=0.000001
        let derivative=(rate_equation(guess+step,periods,payment,present,future,kind)-value)/step;
        if (abs(value)<0.00000001) guess else if (derivative==0) raise error("#NUM!") else {
            let next=guess-value/derivative;
            if (next is nan or next is inf or next == -inf or next<= -1) raise error("#NUM!")
            else if (abs(next-guess)<0.0000001) next else rate_root(periods,payment,present,future,kind,next,iteration+1)^
        }
    }
}
pub fn rate(periods,payment,present,future=0,kind=0,guess=0.1) any^ {
    let n=c.numeric(periods)^
    let p=c.numeric(payment)^
    let pv=c.numeric(present)^
    let fv=c.numeric(if (future==null) 0 else future)^
    let t=c.numeric(if (kind==null) 0 else kind)^
    let g=c.numeric(if (guess==null) 0.1 else guess)^;
    if (n<=0 or not contains([0,1],t)) raise error("#NUM!") else rate_root(n,p,pv,fv,t,g)^
}
