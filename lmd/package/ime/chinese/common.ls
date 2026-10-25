// S11.4.2/S17.8.1: package failures propagate and option names are checked.
pub fn failure(message) error => error({code:7830,message:"Chinese IME: " ++ message})
pub fn require(ok,message) bool^ => if (ok) true else raise failure(message)
pub fn get(value,key,fallback) => if (value[key] == null) fallback else value[key]
pub fn options(value,allowed) map^ {
    let opts = if (value == null) {} else value
    let valid = require(opts is map,"options must be a map")^
    let unknown = [for (key,item in opts where not (string(key) in allowed)) string(key)]
    let names = require(len(unknown) == 0,"unknown option: " ++ join(unknown,", ") ++
        "; supported: " ++ join(allowed,", "))^;
    opts
}
pub fn bounded(value,lo,hi,label) int^ {
    let valid = require(value is int and value >= lo and value <= hi,
        label ++ " must be an integer in " ++ string(lo) ++ ".." ++ string(hi))^;
    value
}
