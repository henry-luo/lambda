// LR10-10 / S7.4.4: an error carries code, message, source location and the
// error it wraps; `error(msg, source)` and `error({..., source})` build a chain.
let inner = error("inner")
let outer = error("outer", inner)
let chained = [outer.message, outer.source.message, outer.source is error, outer.source.source]
chained

// each error() call site is stamped onto the error it constructs
let sites = [inner.line, inner.column, outer.line, outer.column, ends_with(outer.file, "error_members.ls")]
sites

// a non-error source wraps nothing; an unknown member still propagates
let plain = error("plain", 5)
let loose = [plain.source, plain.message, plain.nope is error]
loose

// the parameter map may name the wrapped error too
let mapped = error({code: 404, message: "missing", source: outer})
let from_map = [mapped.code, mapped.message, mapped.source.message, mapped.source.source.message]
from_map

// an error stored in a map field keeps its payload
let holder = {why: outer}
let stored = [holder.why.message, holder.why.line, holder.why.source.message]
stored
