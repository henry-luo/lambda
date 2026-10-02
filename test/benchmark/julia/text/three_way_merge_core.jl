# native port of test/benchmark/text/python/three_way_merge.py; see ../LICENSE.md.
# Line and word three-way merge workload from three_way_merge.js.
const ROUNDS = 11000
const LINE_COUNT = 768
const MODULUS = 1000000007
function make_variant(base, side)
    local index, line, lines
    lines = Any[]
    for (index, line) in enumerate0(base)
        if truth0(((mod(index, 17) == 0)))
            line = add0(line, string(" ", string(side), " edit ", string(mod(index, 31)), " keeps the paragraph useful"))
        else
            if truth0((let _bool_value = ((side == "left")); truth0(_bool_value) ? ((mod(index, 23) == 0)) : _bool_value end))
                line = add0(line, " left-only annotation")
            else
                if truth0((let _bool_value = ((side == "right")); truth0(_bool_value) ? ((mod(index, 29) == 0)) : _bool_value end))
                    line = add0(line, " right-only annotation")
                end
            end
        end
        m_append(lines, line)
    end
    return lines
end

function word_at(words, index)
    return (truth0(((index < Base.length(words)))) ? get0(words, index) : "")
end

function merge_words(base_line, left_line, right_line)
    local base_word, base_words, index, left_word, left_words, right_word, right_words, words
    if truth0(((left_line == right_line)))
        return left_line
    end
    if truth0(((left_line == base_line)))
        return right_line
    end
    if truth0(((right_line == base_line)))
        return left_line
    end
    base_words = m_split(base_line, " ")
    left_words = m_split(left_line, " ")
    right_words = m_split(right_line, " ")
    words = Any[]
    for index in range0(max(Base.length(base_words), Base.length(left_words), Base.length(right_words)))
        base_word = word_at(base_words, index)
        left_word = word_at(left_words, index)
        right_word = word_at(right_words, index)
        if truth0(((left_word == right_word)))
            m_append(words, left_word)
        else
            if truth0(((left_word == base_word)))
                m_append(words, right_word)
            else
                if truth0(((right_word == base_word)))
                    m_append(words, left_word)
                else
                    m_extend(words, ("<<<<<<< LEFT", left_word, "=======", right_word, ">>>>>>> RIGHT"))
                end
            end
        end
    end
    return m_join(" ", words)
end

function merge_lines(base, left, right)
    local base_line, left_line, merged, right_line
    merged = Any[]
    for (base_line, left_line, right_line) in zip(base, left, right)
        if truth0(((left_line == right_line)))
            m_append(merged, left_line)
        else
            if truth0(((left_line == base_line)))
                m_append(merged, right_line)
            else
                if truth0(((right_line == base_line)))
                    m_append(merged, left_line)
                else
                    m_append(merged, merge_words(base_line, left_line, right_line))
                end
            end
        end
    end
    return m_join("\n", merged)
end

