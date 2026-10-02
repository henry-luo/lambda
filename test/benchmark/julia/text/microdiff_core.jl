# native port of test/benchmark/text/python/microdiff.py; see ../LICENSE.md.
# Recursive document diff workload from microdiff.js.
const ROUNDS = 512
const MODULUS = 1000000007
abstract type A_RichValue end
mutable struct C_RichValue <: A_RichValue
    kind
    value
    C_RichValue(::Val{:raw}) = new(nothing, nothing)
end
function C_RichValue(args...)
    self = C_RichValue(Val(:raw))
    init_RichValue(self, args...)
    return self
end

const C_RichValue____slots__ = ("kind", "value")
function init_RichValue(self, kind, value)
    self.kind = kind
    self.value = value
    return nothing
end

function diff(old, new, stack=())
    local compatible, difference, differences, key, new_is_array, new_items, new_value, old_is_array, old_items, old_value, parent, path_key
    differences = Any[]
    old_is_array = (old isa AbstractVector)
    old_items = (truth0(old_is_array) ? enumerate0(old) : m_items(old))
    for (key, old_value) in old_items
        path_key = key
        if truth0((truth0(old_is_array) ? ((key >= Base.length(new))) : (!in0(key, new))))
            m_append(differences, Dict{String,Any}("type"=>"REMOVE", "path"=>Any[path_key], "oldValue"=>old_value))
            continue
        end
        new_value = get0(new, key)
        compatible = (let _bool_value = (old_value isa Union{AbstractDict,AbstractVector,A_RichValue}); truth0(_bool_value) ? (let _bool_value = (new_value isa Union{AbstractDict,AbstractVector,A_RichValue}); truth0(_bool_value) ? (((old_value isa AbstractVector) == (new_value isa AbstractVector))) : _bool_value end) : _bool_value end)
        if truth0((let _bool_value = ((old_value !== nothing)); truth0(_bool_value) ? (let _bool_value = ((new_value !== nothing)); truth0(_bool_value) ? (let _bool_value = compatible; truth0(_bool_value) ? (let _bool_value = !truth0((old_value isa A_RichValue)); truth0(_bool_value) ? all((((objectid(old_value) != objectid(parent))) for parent in stack)) : _bool_value end) : _bool_value end) : _bool_value end) : _bool_value end))
            for difference in diff(old_value, new_value, add0(stack, (old_value,)))
                m_insert(get0(difference, "path"), 0, path_key)
                m_append(differences, difference)
            end
        else
            if truth0(!truth0(values_equal(old_value, new_value)))
                m_append(differences, Dict{String,Any}("type"=>"CHANGE", "path"=>Any[path_key], "value"=>new_value, "oldValue"=>old_value))
            end
        end
    end
    new_is_array = (new isa AbstractVector)
    new_items = (truth0(new_is_array) ? enumerate0(new) : m_items(new))
    for (key, new_value) in new_items
        if truth0((truth0(new_is_array) ? ((key >= Base.length(old))) : (!in0(key, old))))
            m_append(differences, Dict{String,Any}("type"=>"CREATE", "path"=>Any[key], "value"=>new_value))
        end
    end
    return differences
end

function values_equal(left, right)
    if truth0((let _bool_value = (left isa A_RichValue); truth0(_bool_value) ? (right isa A_RichValue) : _bool_value end))
        return (let _bool_value = ((left.kind == right.kind)); truth0(_bool_value) ? ((left.value == right.value)) : _bool_value end)
    end
    return (let _bool_value = ((typeof(left) === typeof(right))); truth0(_bool_value) ? ((left == right)) : _bool_value end)
end

function make_snapshot(version)
    return Dict{Any,Any}("document"=>Dict{Any,Any}("title"=>(truth0(version) ? "Text benchmark — revised" : "Text benchmark"), "sections"=>Any[Dict{Any,Any}("id"=>"intro", "blocks"=>Any[Dict{Any,Any}("type"=>"paragraph", "text"=>"A short paragraph of source text."), Dict{Any,Any}("type"=>"code", "language"=>"js", "lines"=>(truth0(version) ? 18 : 12))]), Dict{Any,Any}("id"=>"body", "blocks"=>Any[Dict{Any,Any}("type"=>"heading", "level"=>(truth0(version) ? 2 : 1), "text"=>"Algorithms"), Dict{Any,Any}("type"=>"list", "items"=>(truth0(version) ? Any["diff", "snapshot", "hyphen"] : Any["diff", "snapshot"]))])]), "options"=>Dict{Any,Any}("theme"=>(truth0(version) ? "dark" : "light"), "flags"=>Dict{Any,Any}("trackChanges"=>truth0(version), "preserveWhitespace"=>true)), "tags"=>(truth0(version) ? Any["text", "benchmark", "updated"] : Any["text", "benchmark"]), "updated"=>C_RichValue("Date", (truth0(version) ? 1700000001000 : 1700000000000)), "pattern"=>C_RichValue("RegExp", (truth0(version) ? "/source|text|diff/gi" : "/source|text/g")), "value"=>(truth0(version) ? 42 : 41))
end

