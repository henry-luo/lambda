// shared admission helpers keep demo and package contracts aligned (PG9).
import t: lambda.scene3d.transform
pub fn value(v,fallback) => if (v == null) fallback else v
pub fn bounded(v,lo,hi,whole=false) bool => t.numeric(v) and (not whole or v is int) and v >= lo and v <= hi
pub fn identifier(v) bool => v is string and len(v) > 0 and len(v) <= 32 and
    all([for (c in split(v,"")) contains("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_",c)])
pub fn fail(message) error => error("procart: " ++ message)
pub fn require(ok:bool,message:string) bool^ => if (not ok) raise fail(message) else true
pub fn fields(v,allowed) bool => all([for (key,item in (if (v is element) map(v) else v)) contains(allowed,string(key))])
pub fn identity() array => [1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0]
