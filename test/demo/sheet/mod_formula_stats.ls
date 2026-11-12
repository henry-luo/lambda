// Descriptive statistics consume the same direct-argument and range coercion as SUM.
import c: .mod_formula_common
fn variance(args,sample,standard_deviation) any^ {
    let values=c.numbers(args)^
    let n=len(values);
    if (n<(if (sample) 2 else 1)) raise error("#DIV/0!") else {
        let mean=sum(values)/n
        let result=sum([for (value in values) (value-mean)*(value-mean)])/(n-(if (sample) 1 else 0));
        if (standard_deviation) sqrt(result) else result
    }
}
pub fn var_s(...) any^ => variance(varg(),true,false)^
pub fn var_p(...) any^ => variance(varg(),false,false)^
pub fn stdev_s(...) any^ => variance(varg(),true,true)^
pub fn stdev_p(...) any^ => variance(varg(),false,true)^
fn ordered(input) => sort([for (value in c.range_values(input) where value is number) value])
pub fn large(input,k) any^ {
    let values=ordered(input)
    let position=trunc(c.numeric(k)^);
    if (position<1 or position>len(values)) raise error("#NUM!") else values[len(values)-position]
}
pub fn small(input,k) any^ {
    let values=ordered(input)
    let position=trunc(c.numeric(k)^);
    if (position<1 or position>len(values)) raise error("#NUM!") else values[position-1]
}
pub fn percentile(input,k) any^ {
    let values=ordered(input)
    let n=len(values)
    let fraction=c.numeric(k)^;
    if (n==0 or fraction<0 or fraction>1) raise error("#NUM!") else {
        let at=(n-1)*fraction
        let low=int(floor(at))
        let high=int(ceil(at));
        values[low]+(values[high]-values[low])*(at-low)
    }
}
pub fn quartile(input,quart) any^ {
    let n=trunc(c.numeric(quart)^);
    if (n<0 or n>4) raise error("#NUM!") else percentile(input,n/4)^
}
pub fn rank(value,input,order=0) any^ {
    let n=c.numeric(value)^
    let ascending=c.numeric(order)^!=0
    let values=ordered(input)
    let candidates=[for (v in values where v==n) v];
    if (len(candidates)==0) raise error("#N/A") else 1+len([for (v in values where if (ascending) v<n else v>n) v])
}

// Paired statistics discard a pair when either referenced cell is nonnumeric.
fn pairs(y,x) any^ {
    let ys=c.range_values(y)
    let xs=c.range_values(x);
    if (len(ys)!=len(xs)) raise error("#N/A") else [for (i,v in ys where v is number and xs[i] is number) {y:v,x:xs[i]}]
}
fn moments(y,x) any^ {
    let data=pairs(y,x)^
    let n=len(data);
    if (n==0) raise error("#DIV/0!") else {
        let mx=sum([for (p in data) p.x])/n
        let my=sum([for (p in data) p.y])/n;
        {n:n,mx:mx,my:my,xx:sum([for (p in data) (p.x-mx)*(p.x-mx)]),
            yy:sum([for (p in data) (p.y-my)*(p.y-my)]),xy:sum([for (p in data) (p.x-mx)*(p.y-my)])}
    }
}
pub fn slope(y,x) any^ { let m=moments(y,x)^; if (m.xx==0) raise error("#DIV/0!") else m.xy/m.xx }
pub fn intercept(y,x) any^ { let m=moments(y,x)^; if (m.xx==0) raise error("#DIV/0!") else m.my-m.xy/m.xx*m.mx }
pub fn forecast(value,y,x) any^ { let m=moments(y,x)^; if (m.xx==0) raise error("#DIV/0!") else m.my+m.xy/m.xx*(c.numeric(value)^-m.mx) }
pub fn correl(y,x) any^ { let m=moments(y,x)^; if (m.xx==0 or m.yy==0) raise error("#DIV/0!") else m.xy/sqrt(m.xx*m.yy) }
pub fn rsq(y,x) any^ { let r=correl(y,x)^; r*r }
pub fn covariance_p(y,x) any^ { let m=moments(y,x)^; m.xy/m.n }
pub fn covariance_s(y,x) any^ { let m=moments(y,x)^; if (m.n<2) raise error("#DIV/0!") else m.xy/(m.n-1) }
pub fn steyx(y,x) any^ {
    let m=moments(y,x)^;
    if (m.n<3 or m.xx==0) raise error("#DIV/0!") else sqrt(max(0,(m.yy-m.xy*m.xy/m.xx)/(m.n-2)))
}
pub fn percentile_exc(input,k) any^ {
    let values=ordered(input)
    let n=len(values)
    let fraction=c.numeric(k)^
    let at=(n+1)*fraction;
    if (n==0 or fraction<=0 or fraction>=1 or at<1 or at>n) raise error("#NUM!") else {
        let low=int(floor(at))
        let high=int(ceil(at));
        values[low-1]+(values[high-1]-values[low-1])*(at-low)
    }
}
pub fn quartile_exc(input,quart) any^ {
    let n=trunc(c.numeric(quart)^);
    if (n<1 or n>3) raise error("#NUM!") else percentile_exc(input,n/4)^
}
pub fn rank_avg(value,input,order=0) any^ {
    let n=c.numeric(value)^
    let values=ordered(input)
    let first=rank(n,input,order)^
    let equal_count=len([for (v in values where v==n) v]);
    first+(equal_count-1)/2
}
pub fn geomean(...) any^ {
    let values=c.numbers(varg())^;
    if (len(values)==0 or any([for (v in values) v<=0])) raise error("#NUM!")
    else math.exp(sum([for (v in values) math.log(v)])/len(values))
}
pub fn harmean(...) any^ {
    let values=c.numbers(varg())^;
    if (len(values)==0 or any([for (v in values) v<=0])) raise error("#NUM!")
    else len(values)/sum([for (v in values) 1/v])
}
