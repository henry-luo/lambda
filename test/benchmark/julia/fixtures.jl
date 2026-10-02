# fixture loading uses the existing benchmark JSON parser outside execution timing.
include("awfy/som.jl")
include("awfy/json_core.jl")

function fixture_value(value)
    if value isa C__JsonObject
        return Dict{String,Any}(m_at(value._names, i) => fixture_value(m_at(value._values, i))
                                for i in 0:m_size(value)-1)
    elseif value isa C__JsonArray
        return [fixture_value(m_get(value, i)) for i in 0:m_size(value)-1]
    elseif value isa C__JsonNumber
        return occursin(r"[.eE]", value._string) ? parse(Float64, value._string) : parse(Int, value._string)
    elseif value isa C__JsonString
        return value._string
    end
    return m_is_null(value) ? nothing : m_is_true(value)
end
read_fixture(name) = fixture_value(m_parse(C__Parser(read(joinpath(@__DIR__, "..", "text", name), String))))
