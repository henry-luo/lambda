// D7.2.4v4: shared pure search for ordered package tables.
// project encoded keys on demand without allocating a parallel key array.
pub fn lower_bound(values,value,lo = 0,hi = null,key = null) int {
    let end = if (hi == null) len(values) else hi
    let middle = (lo + end) div 2;
    if (lo >= end) lo
    else if ((if (key == null) values[middle] else key(values[middle])) < value)
        lower_bound(values,value,middle + 1,end,key)
    else lower_bound(values,value,lo,middle,key)
}
