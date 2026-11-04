// Helpers for the demos whose documents are plain values (Greetings' card,
// Dreamhome's home): an undo history of whole snapshots and file names.

import shared_history: lambda.editor.core.history

// keep the demo's history bound while sharing the snapshot stack policy.
pub let history = shared_history.snapshot_new
pub fn record(h, before) => shared_history.snapshot_record(h,before,60)
pub let undo_step = shared_history.snapshot_undo
pub let redo_step = shared_history.snapshot_redo

// ---- files ----------------------------------------------------------------------

// a title as a file name: lower-case letters and digits joined by dashes
pub fn slug(title, fallback = "untitled") any^ => (let s = join([for (ch in ((split(lower(trim(title)), "")) ^ { [] }))
        if (contains("abcdefghijklmnopqrstuvwxyz0123456789", ch)) ch else "-"], ""),
    let parts = [for (p in ((split(s, "-")) ^ { [] }) where p != "") p], if (len(parts) == 0) fallback else join(parts, "-"))
