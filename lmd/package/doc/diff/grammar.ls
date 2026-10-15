// GM3/GM11v2: whitespace and payload bytes are significant; spans locate hidden payloads.
pub grammar patch {
    Patch = (GitHeader | HunkHeader | OldPath | NewPath | Metadata | BinaryHeader |
             Combined | NoNewline | Context | Added | Removed | Empty | Unknown)*
    GitHeader = _git, paths: header_text, _end
    HunkHeader = _open, old: Range, _new, new: Range, _close, _header_tail?, _end
    Range = line: number, (_comma, count: number)?
    OldPath = _old_path, path: header_text, _end
    NewPath = _new_path, path: header_text, _end
    Metadata = kind: metadata_prefix, value: header_text, _end
    BinaryHeader = _binary_files, _end | patch: binary_patch, _end
    Combined = _combined, _header_tail, _end
    NoNewline = _no_newline, _end
    Context = _context, _payload, _end
    Added = _added, _payload, _end
    Removed = _removed, _payload, _end
    Empty = _lf
    Unknown = _unknown, _end

    number := "\d"+
    header_text := !("\r" | "\n")+
    metadata_prefix := "index " | "old mode " | "new mode " | "new file mode " |
        "deleted file mode " | "rename from " | "rename to " | "copy from " |
        "copy to " | "similarity index " | "dissimilarity index "
    _header_tail := !("\r" | "\n")*
    _payload := !"\n"*
    _unknown := !"\n"+
    _git := "diff --git "
    _old_path := "--- "
    _new_path := "+++ "
    _open := "@@ -"
    _new := " +"
    _close := " @@"
    _comma := ","
    _context := " "
    _added := "+"
    _removed := "-"
    _binary_files := "Binary files " !("\r" | "\n")* " differ"
    binary_patch := "GIT binary patch"
    _combined := "diff --cc " | "diff --combined " | "@@@ "
    _no_newline := "\\ No newline at end of file"
    _lf := "\n"
    _cr := "\r"
    _any := "\n" | !"\n"
    _end = _cr?, _lf | !_any
}

pub grammar paths {
    Pair = before: Path, _gap, after: Path
    Path = Quoted | Bare
    Quoted = _quote, (Run | Escape)*, _quote
    Run = value: plain
    Escape = _slash, value: escaped
    Bare = value: bare
    plain := !("\"" | "\\")+
    escaped := "\d"{3} | .
    bare := !(" " | "\t" | "\r" | "\n" | "\"")+
    _quote := "\""
    _slash := "\\"
    _gap := " "
}

// NUL-delimited Git metadata is parsed as bytes, including non-UTF-8 paths.
pub grammar raw {
    Manifest = (Renamed | Changed)*
    Renamed = Fields, status: renamed, _nul, before: Path, after: Path
    Changed = Fields, status: changed, _nul, path: Path
    Fields = _colon, old_mode: mode, _gap, new_mode: mode, _gap,
        old_oid: oid, _gap, new_oid: oid, _gap
    Path = value: path_bytes, _nul
    mode := "\d"{6}
    oid := ("\d" | "a" to "f")+
    renamed := ("R" | "C") "\d"+
    changed := "A" | "D" | "M" "\d"* | "T" | "U" | "X" | "B"
    path_bytes := !"\u0000"+
    _colon := ":"
    _gap := " "
    _nul := "\u0000"
}

pub grammar path_list {
    Paths = Path*
    Path = value: path_bytes, _nul
    path_bytes := !"\u0000"+
    _nul := "\u0000"
}
