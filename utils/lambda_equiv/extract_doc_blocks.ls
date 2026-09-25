// Native Lambda entry point for utils/extract_doc_blocks.py.
import .extract_doc_blocks_core

pn main() {
    io.mkdir("temp/docblocks")^
    for (path in \.temp.docblocks.*) {
        if (path.is_file and ends_with(path.name, ".ls")) { io.delete(path)^ }
    }
    let report = extract_docs("temp/docblocks", "temp/docblocks_index.json")^
    print("extracted " ++ string(report.written) ++ " unit(s) to temp/docblocks (" ++
          string(report.skipped) ++ " no-run skipped); index: temp/docblocks_index.json\n")
}
