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
