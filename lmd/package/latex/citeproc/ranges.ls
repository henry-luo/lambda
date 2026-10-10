// Expand abbreviated page endpoints before applying the style's range policy.
import c: .common

fn expanded(start, end) {
    if (len(end) >= len(start)) end
    else {
        let prefix = slice(start, 0, len(start) - len(end))
        let candidate = prefix ++ end
        let a = int(start) ^ { null }
        let b = int(candidate) ^ { null }
        if (a != null and b != null and b < a)
            string(b + int(pow(10, len(end)))) else candidate
    }
}

fn common_prefix(a, b, index) {
    if (index >= len(a) or index >= len(b) or slice(a, index, index + 1) != slice(b, index, index + 1)) index
    else common_prefix(a, b, index + 1)
}

fn endpoint(start, end, policy) {
    let full = expanded(start, end)
    let common = common_prefix(start, full, 0)
    let a = int(start) ^ { null }
    let b = int(full) ^ { null }
    if (policy == null or policy == "expanded" or a == null or b == null or len(start) != len(full) or
        (starts_with(policy, "chicago") and len(full) == 4 and common < 2)) full
    else {
        let chicago = starts_with(policy, "chicago")
        let keep = if (policy == "minimal-two") 2
            else if (chicago and (a < 100 or a % 100 == 0)) len(full)
            else if (chicago and a % 100 < 10) 1 else if (chicago) 2 else 1
        let at = min([common, max([0, len(full) - keep])])
        slice(full, at, len(full))
    }
}

pub fn render(value, policy = null, delimiter = "–") {
    let source = replace(replace(c.text(value), "--", "–"), "-", "–")
    let ranges = split(source, ",")
    if (contains(source, "&")) join([for (part in split(source, "&")) render(trim(part), policy, delimiter)], " & ")
    else join([for (passage in ranges) {
        let pair = split(trim(passage), "–")
        if (len(pair) == 2 and (int(pair[0]) ^ { null }) != null and (int(pair[1]) ^ { null }) != null)
            pair[0] ++ delimiter ++ endpoint(pair[0], pair[1], policy)
        else replace(trim(passage), "–", delimiter)
    }], ", ")
}
