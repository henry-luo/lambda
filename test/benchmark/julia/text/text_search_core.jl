# native port of test/benchmark/text/python/text_search.py; see ../LICENSE.md.
# Naive, KMP, and Boyer–Moore search workload from text_search.js.
const ROUNDS = 1536
const MODULUS = 1000000007
function naive_search(text, pattern, start)
    local offset, position
    if truth0(!truth0(pattern))
        return start
    end
    for position in range0(start, add0((Base.length(text) - Base.length(pattern)), 1))
        offset = 0
        while truth0((let _bool_value = ((offset < Base.length(pattern))); truth0(_bool_value) ? ((get0(text, add0(position, offset)) == get0(pattern, offset))) : _bool_value end))
            offset = add0(offset, 1)
        end
        if truth0(((offset == Base.length(pattern))))
            return position
        end
    end
    return -(1)
end

function prefix_table(pattern)
    local index, length, table
    table = mul0([0], Base.length(pattern))
    length = 0
    index = 1
    while truth0(((index < Base.length(pattern))))
        if truth0(((get0(pattern, index) == get0(pattern, length))))
            length = add0(length, 1)
            set0!(table, index, length)
            index = add0(index, 1)
        else
            if truth0(((length > 0)))
                length = get0(table, (length - 1))
            else
                index = add0(index, 1)
            end
        end
    end
    return table
end

function kmp_search(text, pattern, start)
    local pattern_index, table, text_index
    if truth0(!truth0(pattern))
        return start
    end
    table = prefix_table(pattern)
    text_index = start
    pattern_index = 0
    while truth0(((text_index < Base.length(text))))
        if truth0(((get0(text, text_index) == get0(pattern, pattern_index))))
            text_index = add0(text_index, 1)
            pattern_index = add0(pattern_index, 1)
            if truth0(((pattern_index == Base.length(pattern))))
                return (text_index - Base.length(pattern))
            end
        else
            if truth0(((pattern_index > 0)))
                pattern_index = get0(table, (pattern_index - 1))
            else
                text_index = add0(text_index, 1)
            end
        end
    end
    return -(1)
end

function boyer_moore_search(text, pattern, start)
    local code, index, last, offset, position
    if truth0(!truth0(pattern))
        return start
    end
    last = mul0(Any[-(1)], 256)
    for (index, code) in enumerate0(pattern)
        set0!(last, code, index)
    end
    position = start
    while truth0(((position <= (Base.length(text) - Base.length(pattern)))))
        offset = (Base.length(pattern) - 1)
        while truth0((let _bool_value = ((offset >= 0)); truth0(_bool_value) ? ((get0(text, add0(position, offset)) == get0(pattern, offset))) : _bool_value end))
            offset = (offset - 1)
        end
        if truth0(((offset < 0)))
            return position
        end
        position = add0(position, max(1, (offset - get0(last, get0(text, add0(position, offset))))))
    end
    return -(1)
end

