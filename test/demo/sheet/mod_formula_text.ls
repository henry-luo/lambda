// Text functions work on Lambda strings and preserve Excel's 1-based positions.
import c: .mod_formula_common
import criteria: .mod_formula_criteria
pub fn exact(a,b) any^ => c.text(a)^==c.text(b)^
pub fn value(input) any^ => float(c.text(input)^) ^ {raise error("#VALUE!")}
pub fn rept(input,count) any^ {
    let value=c.text(input)^
    let n=trunc(c.numeric(count)^);
    if (n<0 or n*len(value)>32767) raise error("#VALUE!") else join(fill(n,value),"")
}
pub fn clean(input) any^ {
    let value=c.text(input)^;
    join([for (ch in ((split(value,"")) ^ { [] }) where ((ord(ch)>=32) ^ { false })) ch],"")
}
pub fn replace_text(original,start,count,replacement) any^ {
    let value=c.text(original)^
    let at=trunc(c.numeric(start)^)
    let n=trunc(c.numeric(count)^);
    if (at<1 or n<0) raise error("#VALUE!")
    else slice(value,0,at-1) ++ c.text(replacement)^ ++ slice(value,at-1+n)
}
pub fn substitute(original,old,new_text,instance=null) any^ {
    let value=c.text(original)^
    let find=c.text(old)^
    let replacement=c.text(new_text)^
    let n=if (instance==null) null else trunc(c.numeric(instance)^);
    if (n!=null and n<=0) raise error("#VALUE!")
    else if (find=="") value
    else {
        let parts=split(value,find)^;
        join([for (i,part in parts) (if (i==0) "" else if (n==null or i==n) replacement else find) ++ part],"")
    }
}
pub fn find_text(needle,input,start=1) any^ {
    let target=c.text(needle)^
    let value=c.text(input)^
    let at=trunc(c.numeric(start)^);
    if (at<1 or at>len(value)+1) raise error("#VALUE!")
    else { let found=index_of(slice(value,at-1),target); if (found==null) raise error("#VALUE!") else found+at }
}
pub fn search(needle,input,start=1) any^ {
    let target=c.text(needle)^
    let value=c.text(input)^
    let at=trunc(c.numeric(start)^);
    if (at<1 or at>len(value)+1) raise error("#VALUE!")
    else if (not (contains(target,"*") or contains(target,"?") or contains(target,"~"))) {
        let found=index_of(lower(slice(value,at-1)),lower(target));
        if (found==null) raise error("#VALUE!") else found+at
    } else wildcard_search(target,value,at)^
}
fn wildcard_search(target,value,at) any^ {
    var index=at-1
    var matched=false
    while ((index<=len(value) and not matched)^) {
        matched=criteria.wildcard(target,slice(value,index)^,true)^
        if ((not matched)^) { index=index+1 }
    }
    return if (matched) index+1 else raise error("#VALUE!")
}
pub fn textjoin(...) any^ {
    let args=varg()
    let separator=c.text(args[0])^
    let skip=c.truth(args[1])^
    let values=c.flatten(slice(args,2))^
    let result=join([for (item in values where not skip or (item!=null and item!="")) c.text(item)^],separator);
    if (len(result)>32767) raise error("#VALUE!") else result
}
