// D7.2.4/S12.1.1v3: package computation stays pure; IO is confined to procedures.
pub fn fail(message, offset = null) error => error({code:7810,message:"diff: " ++ message ++
    (if (offset == null) "" else " at byte " ++ string(offset))})
pub fn require(ok,message,offset = null) bool^ => if (ok) true else raise fail(message,offset)
pub fn options(value, allowed) map^ {
    let opts = if (value == null) {} else value
    let valid = require(opts is map,"options must be a map")^
    let names = require(all([for (key,item in opts) string(key) in allowed]),"unknown option")^;
    opts
}
pub fn get(value,key,fallback) => if (value[key] == null) fallback else value[key]
pub fn text(value) string^ => if (value == null) "" else if (value is binary) lambda.sys.parse(value,"text")^ else string(value)
type utf8_text = \("" ...)
pub fn display(value) string^ {
    let decoded = text(value)^;
    if (decoded is utf8_text) decoded else format(binary(value),"mark")
}
pub fn natural(value,offset = null) int^ {
    let source = text(value)^
    let parsed = int(source)^
    let valid = require(parsed >= 0 and parsed <= 9007199254740991,"invalid or excessive line number",offset)^;
    parsed
}
pub fn lower_bound(values,value,lo = 0,hi = null) int {
    let end = if (hi == null) len(values) else hi
    let middle = (lo + end) div 2;
    if (lo >= end) lo
    else if (values[middle] < value) lower_bound(values,value,middle + 1,end)
    else lower_bound(values,value,lo,middle)
}
pub fn kind(node) => string(name(node))
pub fn count_of(interval) => if (interval.count == null) 1 else natural(interval.count,interval.start)^
