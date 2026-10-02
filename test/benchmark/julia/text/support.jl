# native collection and string operations shared by the text algorithm ports.
include("../common.jl")
include("../fixtures.jl")

in0(x, values) = in(x, values)
in0(x, values::AbstractDict) = haskey(values, x)
in0(x::AbstractString, values::AbstractString) = occursin(x, values)
add0(a::Tuple, b::Tuple) = (a..., b...)
truth0(x::AbstractDict) = !isempty(x)
ord0(x::Char) = Int(x)
m_extend(x::AbstractVector, values) = (append!(x, values); nothing)
m_insert(x::AbstractVector, at, value) = (insert!(x, at + 1, value); nothing)
m_pop(x::AbstractVector, at) = splice!(x, at < 0 ? length(x) + at + 1 : at + 1)
m_items(x::AbstractDict) = pairs(x)
m_lower(x::AbstractString) = lowercase(x)
m_startswith(x::AbstractString, prefix) = startswith(x, prefix)
m_endswith(x::AbstractString, suffix) = endswith(x, suffix)
m_isascii(x::AbstractString) = isascii(x)
m_isalnum(x::AbstractString) = all(c->isletter(c) || isdigit(c), x)
m_isspace(x::AbstractString) = all(isspace, x)
m_split(x::AbstractString, separator) = String.(split(x, separator; keepempty=true))
function m_find(x::AbstractString, needle, start=0)
    start > length(x) && return -1
    found = findnext(needle, x, nextind(x, 0, start + 1))
    return found === nothing ? -1 : length(SubString(x, 1, first(found) - 1))
end
function setslice0!(values, lo, hi, replacement)
    start = lo === nothing ? 0 : lo
    stop = hi === nothing ? length(values) : hi
    splice!(values, start+1:stop, replacement)
    return replacement
end
re_search(pattern, source) = match(Regex(pattern), source)
re_match(pattern, source) = match(Regex("^(?:" * pattern * ")"), source)
function json_string(value::AbstractString)
    # the AST fixture's string literals need the same JSON quoting as the printer.
    escaped = replace(value, '\\'=>"\\\\", '"'=>"\\\"", '\n'=>"\\n", '\r'=>"\\r", '\t'=>"\\t")
    return "\"" * escaped * "\""
end
format0(x, spec) = spec == "02d" ? lpad(string(x), 2, '0') : spec == ".3f" ? @sprintf("%.3f", x) : error("unsupported format: " * spec)
