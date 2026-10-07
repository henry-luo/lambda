# Value adapters for the jq VM's zero-based bytecode and immutable jq values.
# Updates copy their container in obj_with/setpath_rec; the VM's own arrays mutate.
using Printf
include("../fixtures.jl")

struct JResult
    ok::Bool
    value::Any
end
Base.@kwdef struct Node
    kind::Int
    op::Int = 0
    a::Any = nothing
    b::Any = nothing
    c::Any = nothing
    d::Any = nothing
    list::Vector{Any} = Any[]
    name::String = ""
    value::Any = nothing
    pvar::Vector{Bool} = Bool[]
end
function with_node(n::Node; kwargs...)
    values = (; (field => getfield(n, field) for field in fieldnames(Node))...)
    return Node(; merge(values, (; kwargs...))...)
end

@inline jget(a::AbstractVector, i::Real) = 0 <= i < length(a) ? a[Int(i) + 1] : nothing
@inline jget(o::AbstractDict, key) = get(o, key, nothing)
@inline jset!(a::AbstractVector, i::Real, value) = (a[Int(i) + 1] = value)
@inline jset!(o::AbstractDict, key, value) = (o[key] = value)
@inline jcat(a::AbstractString, rest::AbstractString...) = string(a, rest...)
@inline jcat(a::AbstractVector, rest::AbstractVector...) = vcat(a, rest...)
@inline jor(a, b) = (value = a(); value === nothing || value === false ? b() : value)
function jrescue(a, b)
    try
        return a()
    catch
        return b()
    end
end
jint(x::Real) = trunc(Int, x)
jord(s::AbstractString) = Int(first(s))
jchr(c::Integer) = string(Char(c))
occursin_reverse(source, needle) = occursin(needle, source)
function jslice(a::AbstractVector, lo::Real, hi::Real=length(a))
    return a[jint(lo)+1:jint(hi)]
end
function jslice(s::AbstractString, lo::Real, hi::Real=length(s))
    start = nextind(s, 0, jint(lo) + 1)
    stop = prevind(s, nextind(s, 0, jint(hi) + 1))
    return String(SubString(s, start, stop))
end
function jstring(x)
    # jq's JSON numbers omit .0 for integral doubles.
    if x isa AbstractFloat && isfinite(x) && isinteger(x) && abs(x) < typemax(Int)
        return string(Int(x))
    end
    return string(x)
end
function jparse(text::AbstractString, format::Symbol)
    @assert format === :json
    return fixture_value(m_parse(C__Parser(String(text))))
end
