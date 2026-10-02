# native port of test/benchmark/text/python/fast_diff.py; see ../LICENSE.md.
# Myers text diff and semantic cleanup workload from fast_diff.js.
#
# The diff algorithm follows the checked-in fast-diff implementation (Apache 2.0,
# derived from Neil Fraser's Diff Match and Patch). The benchmark input is ASCII,
# so Python string indices and JavaScript UTF-16 indices coincide here.
#
const (DELETE, EQUAL, INSERT) = (-(1), 0, 1)
const ROUNDS = 256
const MODULUS = 1000000007
function common_prefix(left, right)
    local index
    index = 0
    while truth0((let _bool_value = ((index < min(Base.length(left), Base.length(right)))); truth0(_bool_value) ? ((get0(left, index) == get0(right, index))) : _bool_value end))
        index = add0(index, 1)
    end
    return index
end

function common_suffix(left, right)
    local index
    index = 0
    while truth0((let _bool_value = ((index < min(Base.length(left), Base.length(right)))); truth0(_bool_value) ? ((get0(left, (-(index) - 1)) == get0(right, (-(index) - 1)))) : _bool_value end))
        index = add0(index, 1)
    end
    return index
end

function common_overlap(left, right)
    local count, length
    count = min(Base.length(left), Base.length(right))
    for length in range0(count, 0, -(1))
        if truth0(m_endswith(left, slice0(right, nothing, length, nothing)))
            return length
        end
    end
    return 0
end

function half_match(left, right)
    local first, long_text, match, second, short_text
    (long_text, short_text) = (truth0(((Base.length(left) > Base.length(right)))) ? (left, right) : (right, left))
    if truth0((let _bool_value = ((Base.length(long_text) < 4)); truth0(_bool_value) ? _bool_value : ((mul0(Base.length(short_text), 2) < Base.length(long_text))) end))
        return nothing
    end
    function find_seed(start)
        local best, middle, position, prefix, seed, suffix
        seed = slice0(long_text, start, add0(start, fld(Base.length(long_text), 4)), nothing)
        position = -(1)
        best = nothing
        while truth0(true)
            position = m_find(short_text, seed, add0(position, 1))
            if truth0(((position < 0)))
                break
            end
            prefix = common_prefix(slice0(long_text, start, nothing, nothing), slice0(short_text, position, nothing, nothing))
            suffix = common_suffix(slice0(long_text, nothing, start, nothing), slice0(short_text, nothing, position, nothing))
            middle = slice0(short_text, (position - suffix), add0(position, prefix), nothing)
            if truth0((let _bool_value = ((best === nothing)); truth0(_bool_value) ? _bool_value : ((Base.length(middle) > Base.length(get0(best, 4)))) end))
                best = (slice0(long_text, nothing, (start - suffix), nothing), slice0(long_text, add0(start, prefix), nothing, nothing), slice0(short_text, nothing, (position - suffix), nothing), slice0(short_text, add0(position, prefix), nothing, nothing), middle)
            end
        end
        return (truth0((let _bool_value = best; truth0(_bool_value) ? ((mul0(Base.length(get0(best, 4)), 2) >= Base.length(long_text))) : _bool_value end)) ? best : nothing)
    end

    first = find_seed(fld(add0(Base.length(long_text), 3), 4))
    second = find_seed(fld(add0(Base.length(long_text), 1), 2))
    match = (truth0((let _bool_value = ((second === nothing)); truth0(_bool_value) ? _bool_value : (let _bool_value = first; truth0(_bool_value) ? ((Base.length(get0(first, 4)) > Base.length(get0(second, 4)))) : _bool_value end) end)) ? first : second)
    if truth0(((match === nothing)))
        return nothing
    end
    return (truth0(((Base.length(left) > Base.length(right)))) ? match : (get0(match, 2), get0(match, 3), get0(match, 0), get0(match, 1), get0(match, 4)))
end

function bisect(left, right)
    local delta, diagonal, distance, first_end, first_start, forward, forward_pos, front, length, m, maximum, n, offset, position, reverse, reverse_pos, second_end, second_start, split_x, split_y, x, y
    (n, m) = (Base.length(left), Base.length(right))
    maximum = fld(add0(add0(n, m), 1), 2)
    offset = maximum
    length = mul0(2, maximum)
    (forward, reverse) = (mul0(Any[-(1)], length), mul0(Any[-(1)], length))
    let _assigned = 0
        set0!(forward, add0(offset, 1), _assigned)
        set0!(reverse, add0(offset, 1), _assigned)
    end
    delta = (n - m)
    front = ((mod(delta, 2) != 0))
    let _assigned = 0
        first_start = _assigned
        first_end = _assigned
        second_start = _assigned
        second_end = _assigned
    end
    for distance in range0(maximum)
        for diagonal in range0(add0(-(distance), first_start), add0((distance - first_end), 1), 2)
            position = add0(offset, diagonal)
            x = (truth0((let _bool_value = ((diagonal == -(distance))); truth0(_bool_value) ? _bool_value : (let _bool_value = ((diagonal != distance)); truth0(_bool_value) ? ((get0(forward, (position - 1)) < get0(forward, add0(position, 1)))) : _bool_value end) end)) ? get0(forward, add0(position, 1)) : add0(get0(forward, (position - 1)), 1))
            y = (x - diagonal)
            while truth0((let _bool_value = ((x < n)); truth0(_bool_value) ? (let _bool_value = ((y < m)); truth0(_bool_value) ? ((get0(left, x) == get0(right, y))) : _bool_value end) : _bool_value end))
                x = add0(x, 1)
                y = add0(y, 1)
            end
            set0!(forward, position, x)
            if truth0(((x > n)))
                first_end = add0(first_end, 2)
            else
                if truth0(((y > m)))
                    first_start = add0(first_start, 2)
                else
                    if truth0(front)
                        reverse_pos = (add0(offset, delta) - diagonal)
                        if truth0((let _bool_value = ((0 <= reverse_pos) && (reverse_pos < length)); truth0(_bool_value) ? (let _bool_value = ((get0(reverse, reverse_pos) != -(1))); truth0(_bool_value) ? ((x >= (n - get0(reverse, reverse_pos)))) : _bool_value end) : _bool_value end))
                            return add0(diff_main(slice0(left, nothing, x, nothing), slice0(right, nothing, y, nothing)), diff_main(slice0(left, x, nothing, nothing), slice0(right, y, nothing, nothing)))
                        end
                    end
                end
            end
        end
        for diagonal in range0(add0(-(distance), second_start), add0((distance - second_end), 1), 2)
            position = add0(offset, diagonal)
            x = (truth0((let _bool_value = ((diagonal == -(distance))); truth0(_bool_value) ? _bool_value : (let _bool_value = ((diagonal != distance)); truth0(_bool_value) ? ((get0(reverse, (position - 1)) < get0(reverse, add0(position, 1)))) : _bool_value end) end)) ? get0(reverse, add0(position, 1)) : add0(get0(reverse, (position - 1)), 1))
            y = (x - diagonal)
            while truth0((let _bool_value = ((x < n)); truth0(_bool_value) ? (let _bool_value = ((y < m)); truth0(_bool_value) ? ((get0(left, ((n - x) - 1)) == get0(right, ((m - y) - 1)))) : _bool_value end) : _bool_value end))
                x = add0(x, 1)
                y = add0(y, 1)
            end
            set0!(reverse, position, x)
            if truth0(((x > n)))
                second_end = add0(second_end, 2)
            else
                if truth0(((y > m)))
                    second_start = add0(second_start, 2)
                else
                    if truth0(!truth0(front))
                        forward_pos = (add0(offset, delta) - diagonal)
                        if truth0((let _bool_value = ((0 <= forward_pos) && (forward_pos < length)); truth0(_bool_value) ? ((get0(forward, forward_pos) != -(1))) : _bool_value end))
                            split_x = get0(forward, forward_pos)
                            split_y = (add0(offset, split_x) - forward_pos)
                            if truth0(((split_x >= (n - x))))
                                return add0(diff_main(slice0(left, nothing, split_x, nothing), slice0(right, nothing, split_y, nothing)), diff_main(slice0(left, split_x, nothing, nothing), slice0(right, split_y, nothing, nothing)))
                            end
                        end
                    end
                end
            end
        end
    end
    return Any[Any[DELETE, left], Any[INSERT, right]]
end

function diff_compute(left, right)
    local kind, long_text, match, position, short_text
    if truth0(!truth0(left))
        return Any[Any[INSERT, right]]
    end
    if truth0(!truth0(right))
        return Any[Any[DELETE, left]]
    end
    (long_text, short_text) = (truth0(((Base.length(left) > Base.length(right)))) ? (left, right) : (right, left))
    position = m_find(long_text, short_text)
    if truth0(((position >= 0)))
        kind = (truth0(((Base.length(left) > Base.length(right)))) ? DELETE : INSERT)
        return Any[Any[kind, slice0(long_text, nothing, position, nothing)], Any[EQUAL, short_text], Any[kind, slice0(long_text, add0(position, Base.length(short_text)), nothing, nothing)]]
    end
    if truth0(((Base.length(short_text) == 1)))
        return Any[Any[DELETE, left], Any[INSERT, right]]
    end
    match = half_match(left, right)
    if truth0(match)
        return add0(add0(diff_main(get0(match, 0), get0(match, 2)), Any[Any[EQUAL, get0(match, 4)]]), diff_main(get0(match, 1), get0(match, 3)))
    end
    return bisect(left, right)
end

function cleanup_merge(diffs)
    local after, before, changed, count, deletes, edit, inserted, inserts, kind, pointer, prefix, previous, removed, replacement, suffix, value
    m_append(diffs, Any[EQUAL, ""])
    let _assigned = 0
        pointer = _assigned
        inserts = _assigned
        deletes = _assigned
    end
    let _assigned = ""
        inserted = _assigned
        removed = _assigned
    end
    while truth0(((pointer < Base.length(diffs))))
        if truth0((let _bool_value = ((pointer < (Base.length(diffs) - 1))); truth0(_bool_value) ? !truth0(get0(get0(diffs, pointer), 1)) : _bool_value end))
            m_pop(diffs, pointer)
            continue
        end
        (kind, value) = get0(diffs, pointer)
        if truth0(((kind == INSERT)))
            inserts = add0(inserts, 1)
            inserted = add0(inserted, value)
            pointer = add0(pointer, 1)
        else
            if truth0(((kind == DELETE)))
                deletes = add0(deletes, 1)
                removed = add0(removed, value)
                pointer = add0(pointer, 1)
            else
                previous = (((pointer - inserts) - deletes) - 1)
                if truth0((let _bool_value = removed; truth0(_bool_value) ? _bool_value : inserted end))
                    if truth0((let _bool_value = removed; truth0(_bool_value) ? inserted : _bool_value end))
                        prefix = common_prefix(inserted, removed)
                        if truth0(prefix)
                            if truth0(((previous >= 0)))
                                set0!(get0(diffs, previous), 1, add0(get0(get0(diffs, previous), 1), slice0(inserted, nothing, prefix, nothing)))
                            else
                                m_insert(diffs, 0, Any[EQUAL, slice0(inserted, nothing, prefix, nothing)])
                                pointer = add0(pointer, 1)
                            end
                            (inserted, removed) = (slice0(inserted, prefix, nothing, nothing), slice0(removed, prefix, nothing, nothing))
                        end
                        suffix = common_suffix(inserted, removed)
                        if truth0(suffix)
                            set0!(get0(diffs, pointer), 1, add0(slice0(inserted, -(suffix), nothing, nothing), get0(get0(diffs, pointer), 1)))
                            (inserted, removed) = (slice0(inserted, nothing, -(suffix), nothing), slice0(removed, nothing, -(suffix), nothing))
                        end
                    end
                    replacement = Any[]
                    if truth0(removed)
                        m_append(replacement, Any[DELETE, removed])
                    end
                    if truth0(inserted)
                        m_append(replacement, Any[INSERT, inserted])
                    end
                    count = add0(inserts, deletes)
                    setslice0!(diffs, (pointer - count), pointer, replacement)
                    pointer = add0((pointer - count), Base.length(replacement))
                end
                if truth0((let _bool_value = pointer; truth0(_bool_value) ? ((get0(get0(diffs, (pointer - 1)), 0) == EQUAL)) : _bool_value end))
                    set0!(get0(diffs, (pointer - 1)), 1, add0(get0(get0(diffs, (pointer - 1)), 1), get0(get0(diffs, pointer), 1)))
                    m_pop(diffs, pointer)
                else
                    pointer = add0(pointer, 1)
                end
                let _assigned = 0
                    inserts = _assigned
                    deletes = _assigned
                end
                let _assigned = ""
                    inserted = _assigned
                    removed = _assigned
                end
            end
        end
    end
    if truth0((let _bool_value = diffs; truth0(_bool_value) ? !truth0(get0(get0(diffs, -(1)), 1)) : _bool_value end))
        m_pop(diffs)
    end
    changed = false
    pointer = 1
    while truth0(((pointer < (Base.length(diffs) - 1))))
        if truth0((let _bool_value = ((get0(get0(diffs, (pointer - 1)), 0) == EQUAL)); truth0(_bool_value) ? ((get0(get0(diffs, add0(pointer, 1)), 0) == EQUAL)) : _bool_value end))
            (before, edit, after) = (get0(get0(diffs, (pointer - 1)), 1), get0(get0(diffs, pointer), 1), get0(get0(diffs, add0(pointer, 1)), 1))
            if truth0(m_endswith(edit, before))
                set0!(get0(diffs, pointer), 1, add0(before, slice0(edit, nothing, -(Base.length(before)), nothing)))
                set0!(get0(diffs, add0(pointer, 1)), 1, add0(before, after))
                m_pop(diffs, (pointer - 1))
                changed = true
            else
                if truth0(m_startswith(edit, after))
                    set0!(get0(diffs, (pointer - 1)), 1, add0(get0(get0(diffs, (pointer - 1)), 1), after))
                    set0!(get0(diffs, pointer), 1, add0(slice0(edit, Base.length(after), nothing, nothing), after))
                    m_pop(diffs, add0(pointer, 1))
                    changed = true
                end
            end
        end
        pointer = add0(pointer, 1)
    end
    if truth0(changed)
        cleanup_merge(diffs)
    end
    return nothing
end

function semantic_score(left, right)
    local first, line_first, line_second, non_first, non_second, second, white_first, white_second
    if truth0((let _bool_value = !truth0(left); truth0(_bool_value) ? _bool_value : !truth0(right) end))
        return 6
    end
    (first, second) = (get0(left, -(1)), get0(right, 0))
    (non_first, non_second) = ((let _bool_value = !truth0(m_isascii(first)); truth0(_bool_value) ? _bool_value : !truth0(m_isalnum(first)) end), (let _bool_value = !truth0(m_isascii(second)); truth0(_bool_value) ? _bool_value : !truth0(m_isalnum(second)) end))
    (white_first, white_second) = ((let _bool_value = non_first; truth0(_bool_value) ? m_isspace(first) : _bool_value end), (let _bool_value = non_second; truth0(_bool_value) ? m_isspace(second) : _bool_value end))
    (line_first, line_second) = ((let _bool_value = white_first; truth0(_bool_value) ? (in0(first, "\r\n")) : _bool_value end), (let _bool_value = white_second; truth0(_bool_value) ? (in0(second, "\r\n")) : _bool_value end))
    if truth0((let _bool_value = (let _bool_value = line_first; truth0(_bool_value) ? re_search("\\n\\r?\\n\$", left) : _bool_value end); truth0(_bool_value) ? _bool_value : (let _bool_value = line_second; truth0(_bool_value) ? re_match("\\r?\\n\\r?\\n", right) : _bool_value end) end))
        return 5
    end
    if truth0((let _bool_value = line_first; truth0(_bool_value) ? _bool_value : line_second end))
        return 4
    end
    if truth0((let _bool_value = non_first; truth0(_bool_value) ? (let _bool_value = !truth0(white_first); truth0(_bool_value) ? white_second : _bool_value end) : _bool_value end))
        return 3
    end
    if truth0((let _bool_value = white_first; truth0(_bool_value) ? _bool_value : white_second end))
        return 2
    end
    if truth0((let _bool_value = non_first; truth0(_bool_value) ? _bool_value : non_second end))
        return 1
    end
    return 0
end

function cleanup_lossless(diffs)
    local best, best_score, common, edit, left, pointer, right, score, suffix
    pointer = 1
    while truth0(((pointer < (Base.length(diffs) - 1))))
        if truth0((let _bool_value = ((get0(get0(diffs, (pointer - 1)), 0) == EQUAL)); truth0(_bool_value) ? ((get0(get0(diffs, add0(pointer, 1)), 0) == EQUAL)) : _bool_value end))
            (left, edit, right) = (get0(get0(diffs, (pointer - 1)), 1), get0(get0(diffs, pointer), 1), get0(get0(diffs, add0(pointer, 1)), 1))
            suffix = common_suffix(left, edit)
            if truth0(suffix)
                common = slice0(edit, -(suffix), nothing, nothing)
                (left, edit, right) = (slice0(left, nothing, -(suffix), nothing), add0(common, slice0(edit, nothing, -(suffix), nothing)), add0(common, right))
            end
            best = (left, edit, right)
            best_score = add0(semantic_score(left, edit), semantic_score(edit, right))
            while truth0((let _bool_value = edit; truth0(_bool_value) ? (let _bool_value = right; truth0(_bool_value) ? ((get0(edit, 0) == get0(right, 0))) : _bool_value end) : _bool_value end))
                (left, edit, right) = (add0(left, get0(edit, 0)), add0(slice0(edit, 1, nothing, nothing), get0(right, 0)), slice0(right, 1, nothing, nothing))
                score = add0(semantic_score(left, edit), semantic_score(edit, right))
                if truth0(((score >= best_score)))
                    (best, best_score) = ((left, edit, right), score)
                end
            end
            if truth0(((get0(get0(diffs, (pointer - 1)), 1) != get0(best, 0))))
                if truth0(get0(best, 0))
                    set0!(get0(diffs, (pointer - 1)), 1, get0(best, 0))
                else
                    m_pop(diffs, (pointer - 1))
                    pointer = (pointer - 1)
                end
                set0!(get0(diffs, pointer), 1, get0(best, 1))
                if truth0(get0(best, 2))
                    set0!(get0(diffs, add0(pointer, 1)), 1, get0(best, 2))
                else
                    m_pop(diffs, add0(pointer, 1))
                    pointer = (pointer - 1)
                end
            end
        end
        pointer = add0(pointer, 1)
    end
    return nothing
end

function cleanup_semantic(diffs)
    local at, changed, deletion_after, deletion_before, equalities, forward, inserted, insertion_after, insertion_before, kind, last, pointer, removed, reverse, value
    equalities = Any[]
    last = nothing
    let _assigned = 0
        insertion_before = _assigned
        deletion_before = _assigned
        insertion_after = _assigned
        deletion_after = _assigned
    end
    pointer = 0
    changed = false
    while truth0(((pointer < Base.length(diffs))))
        (kind, value) = get0(diffs, pointer)
        if truth0(((kind == EQUAL)))
            m_append(equalities, pointer)
            (insertion_before, deletion_before) = (insertion_after, deletion_after)
            let _assigned = 0
                insertion_after = _assigned
                deletion_after = _assigned
            end
            last = value
        else
            if truth0(((kind == INSERT)))
                insertion_after = add0(insertion_after, Base.length(value))
            else
                deletion_after = add0(deletion_after, Base.length(value))
            end
            if truth0((let _bool_value = last; truth0(_bool_value) ? (let _bool_value = ((Base.length(last) <= max(insertion_before, deletion_before))); truth0(_bool_value) ? ((Base.length(last) <= max(insertion_after, deletion_after))) : _bool_value end) : _bool_value end))
                at = get0(equalities, -(1))
                m_insert(diffs, at, Any[DELETE, last])
                set0!(get0(diffs, add0(at, 1)), 0, INSERT)
                m_pop(equalities)
                if truth0(equalities)
                    m_pop(equalities)
                end
                pointer = (truth0(equalities) ? get0(equalities, -(1)) : -(1))
                let _assigned = 0
                    insertion_before = _assigned
                    deletion_before = _assigned
                    insertion_after = _assigned
                    deletion_after = _assigned
                end
                last = nothing
                changed = true
            end
        end
        pointer = add0(pointer, 1)
    end
    if truth0(changed)
        cleanup_merge(diffs)
    end
    cleanup_lossless(diffs)
    pointer = 1
    while truth0(((pointer < Base.length(diffs))))
        if truth0((let _bool_value = ((get0(get0(diffs, (pointer - 1)), 0) == DELETE)); truth0(_bool_value) ? ((get0(get0(diffs, pointer), 0) == INSERT)) : _bool_value end))
            (removed, inserted) = (get0(get0(diffs, (pointer - 1)), 1), get0(get0(diffs, pointer), 1))
            (forward, reverse) = (common_overlap(removed, inserted), common_overlap(inserted, removed))
            if truth0((let _bool_value = ((forward >= reverse)); truth0(_bool_value) ? (let _bool_value = ((mul0(forward, 2) >= Base.length(removed))); truth0(_bool_value) ? _bool_value : ((mul0(forward, 2) >= Base.length(inserted))) end) : _bool_value end))
                m_insert(diffs, pointer, Any[EQUAL, slice0(inserted, nothing, forward, nothing)])
                set0!(get0(diffs, (pointer - 1)), 1, slice0(removed, nothing, -(forward), nothing))
                set0!(get0(diffs, add0(pointer, 1)), 1, slice0(inserted, forward, nothing, nothing))
                pointer = add0(pointer, 1)
            else
                if truth0((let _bool_value = ((reverse > forward)); truth0(_bool_value) ? (let _bool_value = ((mul0(reverse, 2) >= Base.length(removed))); truth0(_bool_value) ? _bool_value : ((mul0(reverse, 2) >= Base.length(inserted))) end) : _bool_value end))
                    m_insert(diffs, pointer, Any[EQUAL, slice0(removed, nothing, reverse, nothing)])
                    set0!(diffs, (pointer - 1), Any[INSERT, slice0(inserted, nothing, -(reverse), nothing)])
                    set0!(diffs, add0(pointer, 1), Any[DELETE, slice0(removed, reverse, nothing, nothing)])
                    pointer = add0(pointer, 1)
                end
            end
            pointer = add0(pointer, 1)
        end
        pointer = add0(pointer, 1)
    end
    return nothing
end

function diff_main(left, right, cleanup=false)
    local diffs, leading, prefix, suffix, trailing
    if truth0(((left == right)))
        return (truth0(left) ? Any[Any[EQUAL, left]] : Any[])
    end
    prefix = common_prefix(left, right)
    leading = slice0(left, nothing, prefix, nothing)
    (left, right) = (slice0(left, prefix, nothing, nothing), slice0(right, prefix, nothing, nothing))
    suffix = common_suffix(left, right)
    trailing = (truth0(suffix) ? slice0(left, -(suffix), nothing, nothing) : "")
    if truth0(suffix)
        (left, right) = (slice0(left, nothing, -(suffix), nothing), slice0(right, nothing, -(suffix), nothing))
    end
    diffs = diff_compute(left, right)
    if truth0(leading)
        m_insert(diffs, 0, Any[EQUAL, leading])
    end
    if truth0(trailing)
        m_append(diffs, Any[EQUAL, trailing])
    end
    cleanup_merge(diffs)
    if truth0(cleanup)
        cleanup_semantic(diffs)
    end
    return diffs
end

