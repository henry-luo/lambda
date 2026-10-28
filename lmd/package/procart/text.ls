// Text generation (Lambda_Pkg_Proc_Art.md §4 procart.text): Tracery-style grammar
// expansion (Kate Compton's Tracery) and order-n character Markov models for names.
// Every choice is keyed by its position in the expansion (PG3), so it is exact-tier.
import rng: .rng

let MAX_DEPTH = 64

// --- Tracery grammars ---
// A rule is text with "#symbol.modifier#" tags and "[name:rules]" actions; a backslash
// escapes the next character. The grammar maps each symbol to a rule or an array of rules.
fn chars_of(s) array => [for (i in 0 to (len(s) - 1)) slice(s, i, i + 1)]

fn unescape(s) => join((let cs = chars_of(s), [for (i, c in cs where c != "\\" or (i > 0 and cs[i - 1] == "\\" and
    not (i > 1 and cs[i - 2] == "\\"))) c]), "")

// split a rule into {text}, {tag} and {action} parts with one scan over its characters.
fn parts_of(rule) array^ {
    let cs = chars_of(rule)
    let escaped = [for (i, c in cs) (let run = len([for (j in 0 to (i - 1) where all([for (m in j to (i - 1)) cs[m] == "\\"])) j]), run % 2 == 1)]
    let final_state = reduce([{parts: [], mode: 'text', start: 0, depth: 0}, *[for (i in 0 to (len(cs) - 1)) i]], (st, i) =>
        (let c = cs[i], let live = not escaped[i],
            if (st.mode == 'text' and live and (c == "#" or c == "["))
                {parts: [*st.parts, {text: unescape(slice(rule, st.start, i))}], mode: if (c == "#") 'tag' else 'action',
                 start: i + 1, depth: if (c == "[") 1 else 0}
            else if (st.mode == 'text') st
            else if (live and c == "[") {*: st, depth: st.depth + 1}
            else if (live and c == "]" and st.mode == 'action' and st.depth == 1)
                {parts: [*st.parts, {action: slice(rule, st.start, i)}], mode: 'text', start: i + 1, depth: 0}
            else if (live and c == "]") {*: st, depth: st.depth - 1}
            else if (live and c == "#" and st.mode == 'tag' and st.depth == 0)
                {parts: [*st.parts, {tag: slice(rule, st.start, i)}], mode: 'text', start: i + 1, depth: 0}
            else st));
    if (final_state.mode != 'text') raise error("procart.text: unclosed " ++ (if (final_state.mode == 'tag') "#tag#" else "[action]") ++ " in \"" ++ rule ++ "\"")
    else [*final_state.parts, {text: unescape(slice(rule, final_state.start, len(rule)))}] |: (not (~.text == ""))
}

// a tag's leading [actions], then symbol.modifier.modifier
fn tag_of(inner) map {
    let cs = chars_of(inner)
    let depth = math.cumsum([for (c in cs) if (c == "[") 1 else if (c == "]") -1 else 0])
    let body_at = (let outside = [for (i, c in cs where depth[i] == 0 and c != "]") i], if (len(outside) == 0) len(cs) else outside[0])
    let closes = [for (i, c in cs where i < body_at and c == "]" and depth[i] == 0) i]
    let opens = [0, *[for (j in closes) j + 1]]
    let path = split(slice(inner, body_at, len(inner)), ".");
    {actions: [for (j, e in closes) slice(inner, opens[j] + 1, e)], head: path[0], modifiers: slice(path, 1, len(path))}
}

fn vowel(c: string) bool => contains("aeiouAEIOU", c)

fn modify(s, m) any^ {
    let n = len(s)
    let tail = if (n > 0) slice(s, n - 1, n) else ""
    let before = if (n > 1) slice(s, n - 2, n - 1) else "";
    if (m == "capitalize") (if (n == 0) s else upper(slice(s, 0, 1)) ++ slice(s, 1, n))
    else if (m == "capitalizeAll") join([for (w in split(s, " ")) if (len(w) == 0) w else upper(slice(w, 0, 1)) ++ slice(w, 1, len(w))], " ")
    else if (m == "s") (if (contains("shx", tail)) s ++ "es" else if (tail == "y" and not vowel(before)) slice(s, 0, n - 1) ++ "ies" else s ++ "s")
    else if (m == "a") (if (n > 2 and lower(slice(s, 0, 1)) == "u" and lower(slice(s, 2, 3)) == "i") "a " ++ s
        else if (n > 0 and vowel(slice(s, 0, 1))) "an " ++ s else "a " ++ s)
    else if (m == "ed") (if (contains("shx", tail)) s ++ "ed" else if (tail == "e") s ++ "d"
        else if (tail == "y" and not vowel(before)) slice(s, 0, n - 1) ++ "ied" else if (tail == "y") s ++ "d" else s ++ "ed")
    else if (m == "inQuotes") "\"" ++ s ++ "\""
    else if (m == "comma") (if (contains(",.!?", tail) and n > 0) s else s ++ ",")
    else raise error("procart.text: unknown modifier ." ++ m)
}

fn options_of(lexicon, vars, head) {
    let pushed = vars[head];
    if (pushed != null and len(pushed) > 0) pushed[len(pushed) - 1]
    else (let rules = lexicon[head], if (rules is string) [rules] else rules)
}

fn with_stack(vars, target, pile) map => map([*[for (key, v at vars where string(key) != target) for (x in [string(key), v]) x], target, pile])

// run "[target:a,b]" (push the expanded options) or "[target:POP]"; returns the new variables.
fn act(lexicon, vars, inner, k: u64, depth: int) any^ {
    let colon = index_of(inner, ":")
    let target = if (colon == null) inner else slice(inner, 0, colon)
    let body = if (colon == null) "" else slice(inner, colon + 1, len(inner))
    let pile = if (vars[target] == null) [] else vars[target];
    if (colon == null) raise error("procart.text: an action is written [target:rules]")
    else if (body == "POP") with_stack(vars, target, slice(pile, 0, max(0, len(pile) - 1)))
    else (let expanded = [for (j, opt in split(body, ",")) flatten_rule(lexicon, vars, opt, rng.at(k, j), depth + 1)^[0]],
        with_stack(vars, target, [*pile, expanded]))
}

// expand one tag: its actions hold only while the tag expands.
fn expand_tag(lexicon, vars, inner, k: u64, depth: int) any^ {
    let tag = tag_of(inner)
    let scoped = reduce([vars, *[for (j, a in tag.actions) [j, a]]], (v, ja) => act(lexicon, v, ja[1], rng.at(k, 2 + ja[0]), depth)^)
    let opts = options_of(lexicon, scoped, tag.head);
    if (opts == null or len(opts) == 0) raise error("procart.text: unknown symbol #" ++ tag.head ++ "#")
    else (let chosen = opts[rng.below(rng.at(k, 0), len(opts))],
        let text = flatten_rule(lexicon, scoped, chosen, rng.at(k, 1), depth + 1)^[0],
        reduce([text, *tag.modifiers], (s, m) => modify(s, m)^))
}

// returns [text, variables]: top-level actions persist for the rest of the rule.
fn flatten_rule(lexicon, vars, rule, k: u64, depth: int) any^ {
    if (depth > MAX_DEPTH) raise error("procart.text: expansion deeper than " ++ string(MAX_DEPTH) ++ " (a recursive rule?)")
    else reduce([["", vars], *[for (i, p in parts_of(rule)^) [i, p]]], (acc, ip) => (let p = ip[1], let kp = rng.at(k, ip[0]),
        if (p.text != null) [acc[0] ++ p.text, acc[1]]
        else if (p.action != null) [acc[0], act(lexicon, acc[1], p.action, kp, depth)^]
        else [acc[0] ++ expand_tag(lexicon, acc[1], p.tag, kp, depth)^, acc[1]]))
}

// expand `rule` (default "#origin#") against `grammar`, a map of symbol to rule(s).
pub fn expand(lexicon, k: u64, rule = "#origin#") string^ => flatten_rule(lexicon, {}, rule, k, 0)^[0]

// --- Markov names ---
fn start_mark() string => chr(2)
fn stop_mark() string => chr(3)

// an order-n model: each context of n characters maps to [[next, count], ...].
pub fn markov(samples, order = 2) map^ {
    if (not (order is int) or order < 1 or order > 8) raise error("procart.text.markov: order must be an int in 1..8")
    else if (len(samples) == 0) raise error("procart.text.markov: needs at least one sample")
    else {
        let rows = [for (s in samples, let seq = [*[for (i in 1 to order) start_mark()], *chars_of(s), stop_mark()])
            for (i in order to (len(seq) - 1)) [join(slice(seq, i - order, i), ""), seq[i]]]
        let table = map([for (r in rows group by r[0] as context into g) for (x in [context,
            [for (n in g group by n[1] as next into same order by next) [next, len(same)]]]) x]);
        {order: order, table: table, samples: samples}
    }
}

fn generate(model, k: u64, limit: int) string {
    let out = reduce([[join([for (i in 1 to model.order) start_mark()], ""), "", false], *[for (i in 0 to limit) i]], (st, i) =>
        if (st[2]) st else (let opts = model.table[st[0]], let next = rng.weighted(rng.at(k, i), opts |> ~[0], opts |> ~[1]),
            if (next == stop_mark()) [st[0], st[1], true]
            else [slice(st[0] ++ next, 1, model.order + 1), st[1] ++ next, false]));
    out[1]
}

// a generated name of min..max characters; novel (default) also rejects the training samples.
pub fn name(k: u64, model, options = {}) string^ {
    let lo = if (options.min == null) 3 else options.min
    let hi = if (options.max == null) 12 else options.max
    let attempts = if (options.attempts == null) 100 else options.attempts
    let novel = options.novel != false
    let found = [for (a in 0 to (attempts - 1), let s = generate(model, rng.at(k, a), hi + 1)
        where len(s) >= lo and len(s) <= hi and not (novel and s in model.samples)) s];
    if (len(found) == 0) raise error("procart.text.name: no acceptable name in " ++ string(attempts) ++ " attempts")
    else found[0]
}
