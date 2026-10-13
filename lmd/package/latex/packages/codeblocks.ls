// fancyvrb and minted: code blocks through the listings renderer
// (vibe/impl/Lambda_Impl_Latex_Packages.md §7). Their environments keep the source
// raw; the head ([options] and minted's {language}) is read here.
import util: ~~.util
import listings: .listings

pub let FANCYVRB_ENVIRONMENTS = ["Verbatim", "BVerbatim", "LVerbatim"]
pub let MINTED_ENVIRONMENTS = ["minted"]

// keys with a listings counterpart; numbers and frames are checked by value
let FANCYVRB_KEYS = ["numbers", "firstnumber", "stepnumber", "frame", "fontsize"]
let MINTED_KEYS = ["linenos", "firstnumber", "stepnumber", "frame", "fontsize", "breaklines"]
let FRAMES = ["none", "single", "lines", "leftline", "topline", "bottomline"]

// {options, language, code} of a raw environment body: [options] first, then
// minted's {language}; the code starts on the following line
pub fn head(source, minted) {
    let text = source or ""
    let options = leading_group(text, "[", "]")
    let after_options = if (options == null) text else options.rest
    let language = if (minted) leading_group(after_options, "{", "}") else null
    let rest = if (language == null) after_options else language.rest
    let line_end = index_of(rest, "\n")
    let code = if (line_end == null) trim(rest) else slice(rest, line_end + 1, len(rest));
    // the line break before \end closes the last line; it does not open another
    {options: if (options == null) null else options.value,
     language: if (language == null) null else trim(language.value),
     code: if (ends_with(code, "\n") or false) slice(code, 0, len(code) - 1) else code}
}

// a group at the start of the text (after spaces): its value and the text after it
fn leading_group(text, opener, closer) {
    let start = len(text) - len(trim_start(text))
    let close_at = if (slice(text, start, start + 1) == opener) index_of(slice(text, start, len(text)), closer) else null;
    (if (close_at == null) null
     else {value: slice(text, start + 1, start + close_at), rest: slice(text, start + close_at + 1, len(text))})
}

// the keys in listings' terms: minted's linenos numbers the lines on the left,
// and every frame style draws a box
pub fn listing_options(keys, minted) {
    let numbers = if (minted) (if (util.option_enabled(keys.linenos)) "left" else null) else keys.numbers;
    {numbers: numbers, firstnumber: keys.firstnumber, stepnumber: keys.stepnumber,
     frame: if (keys.frame == null or keys.frame == "none") null else "single",
     basicstyle: keys.fontsize, breaklines: keys.breaklines}
}

// the keys a block can set: the preamble defaults (\fvset, \setminted) and its own
pub fn keys_of(raw, defaults) => {*:defaults, *:util.parse_kv_options(raw)}

// the diagnostics of a block's keys; minted's language and fancyvrb's command
// characters have no rendering
pub fn issues(keys, minted, language, offset) {
    let known = if (minted) MINTED_KEYS else FANCYVRB_KEYS
    let package = if (minted) "minted" else "fancyvrb"
    let unknown = [for (key, value at keys where not (contains(known, string(key)) or false))
        util.diagnostic("unsupported-listing-option", package, string(key),
            "Unsupported " ++ package ++ " key " ++ string(key), offset)]
    let frame = if (keys.frame == null or (contains(FRAMES, keys.frame) or false)) [] else
        [util.diagnostic("unsupported-listing-option", package, "frame", "Unsupported frame " ++ keys.frame, offset)]
    let partial_frame = if (keys.frame == null or keys.frame == "none" or keys.frame == "single") [] else
        [util.diagnostic("frame-approximation", package, keys.frame,
            "The " ++ keys.frame ++ " frame draws a full box", offset)]
    let highlighting = if (language == null) [] else
        [util.diagnostic("listing-highlighting-omitted", package, language,
            "Syntax highlighting is not implemented", offset)];
    unknown ++ frame ++ partial_frame ++ highlighting
}
