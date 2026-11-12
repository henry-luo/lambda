// Modern lookup functions use scalar return ranges; dynamic arrays are not emitted.
import c: .mod_formula_common
import criteria: .mod_formula_criteria
fn is_range(value) => c.is_array_value(value)
fn vector(value) => is_range(value) and (value.rows==1 or value.cols==1)
fn comparable(a,b) => (a is number and b is number) or (a is string and b is string) or (a is bool and b is bool)
fn exact(key,value,mode) any^ => if (mode==2 and key is string) criteria.wildcard_exact(key,value)^ else c.equal(key,value)
fn best(values,indices,mode,is_reverse) any^ {
    if (len(indices)==0) null else {
        let first=if (is_reverse) indices[last] else indices[0]
        let rest=if (is_reverse) reverse(indices) else indices;
        reduce([first,*rest],(old,i) any^ => if ((mode== -1 and c.greater(values[i],values[old])) or
            (mode==1 and c.less(values[i],values[old]))) i else old)
    }
}
// Excel's binary modes require sorted lookup vectors; do not silently linearize them.
fn binary_index(key,values,mode,descending) any^ {
    if (mode==2) { return raise error("#VALUE!") }
    var low=0
        var high=len(values)-1
        var found=null
        var done=false
        while ((low<=high and not done)^) {
            let mid=int(floor((low+high)/2))
            let item=values[mid]
            if ((not comparable(item,key))^) { return raise error("#N/A") }
            if ((c.equal(item,key))^) { found=mid; done=true }
            else {
                let smaller=c.less(item,key)^
                if (((mode== -1 and smaller) or (mode==1 and not smaller))^) { found=mid }
                if ((smaller!=descending)^) { low=mid+1 } else { high=mid-1 }
            }
        }
    return found
}
fn find_index(key,lookup,match_mode,search_mode) any^ {
    let mode=c.numeric(if (match_mode==null) 0 else match_mode)^
    let search=c.numeric(if (search_mode==null) 1 else search_mode)^;
    if (not vector(lookup) or not contains([-1,0,1,2],mode) or not contains([-2,-1,1,2],search)) raise error("#VALUE!")
    else if (search==2 or search== -2) binary_index(key,lookup.values,mode,search== -2)^
    else {
        let values=lookup.values
        let exacts=[for (i,value in values where ((exact(key,value,mode)) ^ { false })) i];
        if (len(exacts)>0) (if (search== -1) exacts[last] else exacts[0])
        else if (mode==0 or mode==2) null else {
            let candidates=[for (i,value in values where ((comparable(key,value) and
                (if (mode== -1) c.less(value,key) else c.greater(value,key))) ^ { false })) i];
            best(values,candidates,mode,search== -1)^
        }
    }
}
pub fn xmatch(key,lookup,match_mode=0,search_mode=1) any^ {
    let index=find_index(key,lookup,match_mode,search_mode)^;
    if (index==null) raise error("#N/A") else index+1
}
pub fn xlookup(key,lookup,returns,if_not_found=null,match_mode=0,search_mode=1) any^ {
    if (not vector(lookup) or not vector(returns) or lookup.rows!=returns.rows or lookup.cols!=returns.cols) raise error("#VALUE!")
    else {
        let index=find_index(key,lookup,match_mode,search_mode)^;
        if (index!=null) returns.values[index]
        else if (if_not_found==null) raise error("#N/A") else {
            let fallback=call(if_not_found,[])^;
            if (fallback==null) raise error("#N/A") else fallback
        }
    }
}

// LOOKUP uses the last matching entry in an ascending vector.
pub fn lookup(key,lookup_vector,result_vector=null) any^ {
    let result=if (result_vector==null) lookup_vector else result_vector;
    if (not vector(lookup_vector) or not vector(result) or len(lookup_vector.values)!=len(result.values)) raise error("#VALUE!") else {
        let candidates=[for (i,value in lookup_vector.values where ((comparable(key,value) and not c.greater(value,key)) ^ { false })) i];
        if (len(candidates)==0) raise error("#N/A") else result.values[candidates[last]]
    }
}
