// Full target records and exporter artifacts are produced by samples/audit.py.
import audit: ~~.~~.latex.samples.audit

fn sample_record(file) {
    let record = audit.record(file, "html")
    {file: file, parsed: record.parsed,
     rendered: record.unsupported == 0,
     serialized: record.serialization_error == null and record.output_chars > 0,
     lua_boundary: file != "arithmetic_lualatex" or any([for (issue in record.diagnostics)
        issue.code == "unsupported-lua"]),
     located: all([for (issue in record.diagnostics) issue.offset is int]),
     visible: len(record.visible) > 0}
}

[for (file in audit.SAMPLES) sample_record(file)]
