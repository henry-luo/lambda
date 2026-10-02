# native port of test/benchmark/text/python/hyphen.py; see ../LICENSE.md.
# Liang-pattern hyphenation workload from hyphen.js.
const TABLES_PATH = "test/benchmark/text/hyphen_tables.json"
const CASES_PATH = "test/benchmark/text/hyphen_cases.json"
const ROUNDS = 32
const MODULUS = 1000000007
function ascii_letter(char)
    return (let _bool_value = (("A" <= char) && (char <= "Z")); truth0(_bool_value) ? _bool_value : (("a" <= char) && (char <= "z")) end)
end

function word_char(char)
    return (let _bool_value = ascii_letter(char); truth0(_bool_value) ? _bool_value : ((char == "'")) end)
end

abstract type A_Hyphenator end
mutable struct C_Hyphenator <: A_Hyphenator
    tables
    word_cache
    marker_cache
    exceptions
    C_Hyphenator(::Val{:raw}) = new(nothing, nothing, nothing, nothing)
end
function C_Hyphenator(args...)
    self = C_Hyphenator(Val(:raw))
    init_Hyphenator(self, args...)
    return self
end

function init_Hyphenator(self, tables)
    local count, offset, word
    self.tables = tables
    self.word_cache = Dict{Any,Any}()
    self.marker_cache = Dict{Any,Any}()
    self.exceptions = Dict{Any,Any}(word=>slice0(get0(tables, "exception_markers"), offset, add0(offset, count), nothing) for (word, offset, count) in zip(get0(tables, "exception_words"), get0(tables, "exception_offsets"), get0(tables, "exception_counts")))
    return nothing
end

function m_trie_child(self::A_Hyphenator, node, code)
    local edge, first, tables
    tables = self.tables
    first = get0(get0(tables, "node_first"), node)
    for edge in range0(first, add0(first, get0(get0(tables, "node_count"), node)))
        if truth0(((get0(get0(tables, "edge_code"), edge) == code)))
            return get0(get0(tables, "edge_child"), edge)
        end
    end
    return -(1)
end

function m_markers_for_word(self::A_Hyphenator, word)
    local count, cursor, extended, index, length, level_index, level_offset, levels, lowered, markers, node, offset, position, start, tables, target, value
    lowered = m_lower(word)
    if truth0((in0(lowered, self.exceptions)))
        return get0(self.exceptions, lowered)
    end
    if truth0((in0(lowered, self.marker_cache)))
        return get0(self.marker_cache, lowered)
    end
    tables = self.tables
    length = Base.length(word)
    levels = mul0([0], add0(length, 1))
    extended = add0(add0(".", lowered), ".")
    for start in range0(length)
        node = get0(tables, "root")
        position = (truth0(((start == 0))) ? 0 : (start - 1))
        for cursor in range0(start, add0(length, 2))
            node = m_trie_child(self, node, ord0(get0(extended, cursor)))
            if truth0(((node < 0)))
                break
            end
            level_index = get0(get0(tables, "node_level"), node)
            if truth0(((level_index >= 0)))
                offset = get0(get0(tables, "level_offsets"), level_index)
                count = get0(get0(tables, "level_lengths"), level_index)
                for level_offset in range0(count)
                    target = add0(position, level_offset)
                    value = get0(get0(tables, "level_values"), add0(offset, level_offset))
                    if truth0((let _bool_value = ((0 <= target) && (target <= length)); truth0(_bool_value) ? ((value > get0(levels, target))) : _bool_value end))
                        set0!(levels, target, value)
                    end
                end
            end
        end
    end
    let _assigned = 0
        set0!(levels, 0, _assigned)
        set0!(levels, 1, _assigned)
        set0!(levels, (length - 1), _assigned)
        set0!(levels, length, _assigned)
    end
    markers = [index for (index, value) in enumerate0(levels) if truth0((value & 1))]
    set0!(self.marker_cache, lowered, markers)
    return markers
end

function m_hyphenate_word(self::A_Hyphenator, word)
    local _, char, chars, index, marker_index, markers, result
    if truth0((in0(word, self.word_cache)))
        return get0(self.word_cache, word)
    end
    if truth0((let _bool_value = ((Base.length(word) < 5)); truth0(_bool_value) ? _bool_value : (in0("-", word)) end))
        result = word
    else
        markers = m_markers_for_word(self, word)
        marker_index = 0
        chars = Any[]
        for (index, char) in enumerate0(word)
            if truth0((let _bool_value = ((marker_index < Base.length(markers))); truth0(_bool_value) ? ((get0(markers, marker_index) == index)) : _bool_value end))
                m_append(chars, "-")
                marker_index = add0(marker_index, 1)
            end
            m_append(chars, string(char))
        end
        m_extend(chars, ("-" for _ in slice0(markers, marker_index, nothing, nothing)))
        result = m_join("", chars)
    end
    set0!(self.word_cache, word, result)
    return result
end

function m_hyphenate_text(self::A_Hyphenator, source)
    local char, index, j_end, result, start
    result = Any[]
    index = 0
    while truth0(((index < Base.length(source))))
        if truth0((let _bool_value = ((get0(source, index) == "<")); truth0(_bool_value) ? (let _bool_value = ((add0(index, 1) < Base.length(source))); truth0(_bool_value) ? (let _bool_value = ascii_letter(get0(source, add0(index, 1))); truth0(_bool_value) ? _bool_value : ((get0(source, add0(index, 1)) == "/")) end) : _bool_value end) : _bool_value end))
            j_end = m_find(source, ">", index)
            if truth0(((j_end < 0)))
                j_end = (Base.length(source) - 1)
            end
            m_append(result, slice0(source, index, add0(j_end, 1), nothing))
            index = add0(j_end, 1)
        else
            if truth0(word_char(get0(source, index)))
                start = index
                while truth0(((index < Base.length(source))))
                    char = get0(source, index)
                    if truth0(word_char(char))
                        index = add0(index, 1)
                    else
                        if truth0((let _bool_value = ((char == "-")); truth0(_bool_value) ? (let _bool_value = ((index > start)); truth0(_bool_value) ? (let _bool_value = ((add0(index, 1) < Base.length(source))); truth0(_bool_value) ? ascii_letter(get0(source, add0(index, 1))) : _bool_value end) : _bool_value end) : _bool_value end))
                            index = add0(index, 1)
                        else
                            break
                        end
                    end
                end
                m_append(result, m_hyphenate_word(self, slice0(source, start, index, nothing)))
            else
                m_append(result, get0(source, index))
                index = add0(index, 1)
            end
        end
    end
    return m_join("", result)
end

