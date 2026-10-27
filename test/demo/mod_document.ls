// Helpers for the demos whose documents are plain values (Greetings' card,
// Dreamhome's home): an undo history of whole snapshots and file names.

// ---- history ------------------------------------------------------------------
// every edit records the value it replaces, so one undo steps back once

let HISTORY_LIMIT = 60
pub fn history() => {past: [], future: []}
pub fn record(h, before) => (let past = [*h.past, before],
    {past: if (len(past) > HISTORY_LIMIT) slice(past, 1, len(past)) else past, future: []})
pub fn undo_step(h, current) => if (len(h.past) == 0) null
    else {value: h.past[len(h.past) - 1], history: {past: slice(h.past, 0, len(h.past) - 1), future: [current, *h.future]}}
pub fn redo_step(h, current) => if (len(h.future) == 0) null
    else {value: h.future[0], history: {past: [*h.past, current], future: slice(h.future, 1, len(h.future))}}

// ---- files ----------------------------------------------------------------------

// a title as a file name: lower-case letters and digits joined by dashes
pub fn slug(title, fallback = "untitled") => (let s = join([for (ch in split(lower(trim(title)), ""))
        if (contains("abcdefghijklmnopqrstuvwxyz0123456789", ch)) ch else "-"], ""),
    let parts = [for (p in split(s, "-") where p != "") p], if (len(parts) == 0) fallback else join(parts, "-"))
