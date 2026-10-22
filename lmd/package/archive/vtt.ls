// WebVTT (AR4v2): the cues of a track and the transcript text.ls puts in
// text.txt and the media page shows. Voice and style tags are dropped, the
// common entities decoded, and a cue repeating the one before it is said once.

let ENTITIES = [["&lt;", "<"], ["&gt;", ">"], ["&nbsp;", " "], ["&lrm;", ""], ["&rlm;", ""], ["&amp;", "&"]]

fn untag(s) => reduce([replace(s, \("<" (!">")* ">"), ""), *ENTITIES], (acc, e) => replace(acc, e[0], e[1]))
fn blocks(text) => [for (b in split(replace(replace(text or "", "\r\n", "\n"), "\r", "\n"), \("\n" "\n"+)),
    let t = trim(b) where t != "") t]

// the cues: a block with a timing line, its start and end as written and its text
pub fn cues(text) => [for (b in blocks(text), let ls = split(b, "\n"),
    let at = [for (i, l in ls where contains(l, "-->")) i] where len(at) > 0)
    (let timing = split(ls[at[0]], "-->"),
     {start: trim(timing[0]), end: trim(split(trim(timing[1]), " ")[0]),
      text: trim(join([for (l in drop(ls, at[0] + 1)) trim(untag(l))], " "))})]

// one line per cue, in order, a cue equal to the previous one skipped
pub fn transcript(text) {
    let lines = [for (c in cues(text) where c.text != "") c.text];
    join([for (i, l in lines where i == 0 or l != lines[i - 1]) l], "\n")
}
