# adapters retain zero-based indices in the SOM algorithms while storing native Julia values.
truth0(x) = x !== nothing
truth0(x::Bool) = x
truth0(x::Number) = x != 0
truth0(x::AbstractString) = !isempty(x)
truth0(x::AbstractArray) = !isempty(x)
range0(stop) = 0:stop-1
range0(start, stop) = start:stop-1
range0(start, stop, step) = start:step:(stop - sign(step))
enumerate0(x) = ((i - 1, v) for (i, v) in enumerate(x))
int0(x) = trunc(Int, x)
int0(x::AbstractString) = parse(Int, x)
ord0(x) = Int(first(x))
chr0(x) = string(Char(x))

# ASCII admits byte indexing; Unicode strings retain character indexing below.
struct AsciiText
    text::String
end
Base.length(x::AsciiText) = ncodeunits(x.text)
get0(x::AsciiText, i) = string(Char(codeunit(x.text, i < 0 ? length(x) + i + 1 : i + 1)))
function slice0(x::AsciiText, lo=nothing, hi=nothing, step=nothing)
    n = length(x)
    start = lo === nothing ? 0 : clamp(lo < 0 ? n + lo : lo, 0, n)
    stop = hi === nothing ? n : clamp(hi < 0 ? n + hi : hi, 0, n)
    if step === nothing || step == 1
        return stop <= start ? "" : x.text[start+1:stop]
    end
    return join(slice0(collect(x.text), lo, hi, step))
end

get0(x, i) = x[i < 0 ? length(x) + i + 1 : i + 1]
get0(x::AbstractDict, i) = x[i]
function get0(x::AbstractString, i)
    i = i < 0 ? length(x) + i : i
    at = nextind(x, 0, i + 1)
    return string(x[at])
end
function set0!(x, i, v)
    x[i < 0 ? length(x) + i + 1 : i + 1] = v
    return v
end
set0!(x::AbstractDict, i, v) = (x[i] = v)
function slice0(x, lo=nothing, hi=nothing, step=nothing)
    n = length(x)
    stride = step === nothing ? 1 : step
    start = lo === nothing ? (stride > 0 ? 0 : n - 1) : (lo < 0 ? n + lo : lo)
    stop = hi === nothing ? (stride > 0 ? n : -1) : (hi < 0 ? n + hi : hi)
    if stride > 0
        start, stop = clamp(start, 0, n), clamp(stop, 0, n)
    else
        start, stop = clamp(start, -1, n - 1), clamp(stop, -1, n - 1)
    end
    return x[(start + 1):stride:(stop + 1 - sign(stride))]
end
function slice0(x::AbstractString, lo=nothing, hi=nothing, step=nothing)
    if step === nothing || step == 1
        n = length(x)
        start = lo === nothing ? 0 : clamp(lo < 0 ? n + lo : lo, 0, n)
        stop = hi === nothing ? n : clamp(hi < 0 ? n + hi : hi, 0, n)
        stop <= start && return ""
        return String(SubString(x, nextind(x, 0, start + 1), nextind(x, 0, stop)))
    end
    return join(slice0(collect(x), lo, hi, step))
end

add0(a, b) = a + b
add0(a::AbstractString, b::AbstractString) = a * b
add0(a::AbstractVector, b::AbstractVector) = vcat(a, b)
mul0(a, b) = a * b
mul0(a::AbstractString, n::Integer) = repeat(a, n)
mul0(a::AbstractVector, n::Integer) = repeat(a, n)
m_append(x::AbstractVector, value) = (push!(x, value); nothing)
m_pop(x::AbstractVector) = pop!(x)
m_join(separator::AbstractString, values) = Base.join(values, separator)
m_get(x::AbstractDict, key, fallback=nothing) = get(x, key, fallback)
m_compare(f::Function, a, b) = f(a, b)
format0(x, spec) = spec == ".3f" ? @sprintf("%.3f", x) : error("unsupported numeric format: " * spec)
