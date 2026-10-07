# development-time syntax extraction; generated ports do not require Julia.
function json(x)
    if x isa Expr
        return "{\"h\":" * json(string(x.head)) * ",\"a\":[" * join([json(a) for a in x.args if !(a isa LineNumberNode)], ",") * "]}"
    elseif x isa Symbol
        return "{\"s\":" * json(string(x)) * "}"
    elseif x isa QuoteNode
        return "{\"q\":" * json(x.value) * "}"
    elseif x isa LineNumberNode
        return "null"
    elseif x isa Char
        return "{\"c\":" * json(string(x)) * "}"
    elseif x isa AbstractString
        return "\"" * replace(x, '\\'=>"\\\\", '"'=>"\\\"", '\n'=>"\\n", '\r'=>"\\r", '\t'=>"\\t", '\b'=>"\\b", '\f'=>"\\f", '$'=>"\u0024") * "\""
    elseif x === nothing
        return "null"
    elseif x isa Bool
        return string(x)
    elseif x isa Number
        return string(x)
    end
    error("unsupported " * string(typeof(x)))
end
root = abspath(joinpath(@__DIR__, "..", "julia"))
for (dir, _, files) in walkdir(root), file in files
    endswith(file, ".jl") || continue
    path = joinpath(dir, file)
    out = joinpath(ARGS[1], relpath(path, root) * ".json")
    mkpath(dirname(out))
    write(out, json(Meta.parseall(read(path, String))))
end
