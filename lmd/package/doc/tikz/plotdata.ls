// PGFPlots data sources: inline and document-local tables, symbolic
// coordinates and explicit error offsets. Files load only beside the document.
import opts: .options
import util: lambda.latex.util
import paths: lambda.edit.session

let TABLE_KEYS = ["x", "y", "x index", "y index", "col sep", "row sep", "header",
    "x error", "y error", "x error index", "y error index"]

fn children_named(node, tag) => [for (child in node
    where child is element and string(name(child)) == tag) child]

fn table_options(plot) {
    let wrappers = children_named(plot, "table_options")
    if (len(wrappers) == 0) null else wrappers[0]
}

// A file name is a single token; inline tables contain rows or separated cells.
pub fn inline_table(raw) {
    let text = trim(raw)
    contains(text, "\n") or contains(text, "\\\\") or contains(text, " ") or
        contains(text, "\t")
}

// Reject names that would leave the document directory (D7.5.2 path gate).
fn local_name(name) any^ {
    let segments = split(replace(name, "\\", "/"), "/")
    if (name == "" or starts_with(name, "/") or starts_with(name, "~") or
        contains(name, ":") or contains(name, "\\") or
        any([for (segment in segments) segment == ".."]))
        raise error("PGFPlots table file must be beside the document: " ++ name)
    else name
}

// The document directory: an explicit base URI, else the source file's directory.
pub fn resource_base(options) =>
    if (options == null) null
    else if (options.base_uri != null) options.base_uri
    else if (options.source_path != null) paths.dirname(options.source_path)
    else null

pub fn table_text(plot, base_uri) any^ {
    let raw = if (plot.table_source == null) "" else plot.table_source
    if (inline_table(raw)) raw
    else {
        let file_name = local_name(trim(raw))^
        let valid_base = if (base_uri == null or base_uri == "")
            raise error("PGFPlots table file needs a document base URI: " ++ file_name)
            else true
        let text = input(paths.resolve_path(base_uri, file_name), "text") ^ { null }
        if (text == null) raise error("PGFPlots table file not found: " ++ file_name)
        else text
    }
}

fn numeric(cell) => opts.numeric_value(cell) ^ { null }

fn data_rows(text, row_separator) {
    let rows = if (row_separator == "\\\\") split(replace(text, "\n", " "), "\\\\")
        else split(replace(text, "\r", ""), "\n");
    [for (row in rows where trim(row) != "" and not starts_with(trim(row), "%") and
        not starts_with(trim(row), "#")) trim(row)]
}

fn cells(row, separator) {
    if (separator == "space")
        [for (cell in split(replace(row, "\t", " "), " ") where cell != "") cell]
    else {
        let delimiter = if (separator == "comma") ","
            else if (separator == "semicolon") ";" else "\t"
        // A trailing delimiter does not add an empty column.
        let parts = [for (cell in split(row, delimiter)) trim(cell)]
        if (len(parts) > 0 and parts[len(parts) - 1] == "") slice(parts, 0, len(parts) - 1)
        else parts
    }
}

fn column_index(header, raw_name, raw_index, fallback, label) any^ {
    if (raw_name != null) {
        let matches = [for (index, column in header where column == trim(raw_name)) index]
        if (len(matches) == 0) raise error("PGFPlots table has no column " ++ trim(raw_name))
        else matches[0]
    } else if (raw_index != null) {
        let index = opts.numeric_value(raw_index)^
        if (index < 0.0 or float(int(index)) != index or int(index) >= len(header))
            raise error("PGFPlots table " ++ label ++ " index is out of range")
        else int(index)
    } else fallback
}

fn cell_value(row, index) {
    let raw = if (index < len(row)) row[index] else null
    let value = if (raw == null) null else numeric(raw)
    if (raw == null) null
    else if (value != null) {amount: value}
    else {source: raw}
}

fn table_point(row, columns) {
    let x = cell_value(row, columns.x)
    let y = cell_value(row, columns.y)
    let ey = if (columns.y_error == null) null else cell_value(row, columns.y_error)
    let ex = if (columns.x_error == null) null else cell_value(row, columns.x_error);
    {x: if (x == null) null else x.amount, x_source: if (x == null) null else x.source,
     y: if (y == null) null else y.amount, y_source: if (y == null) null else y.source,
     error_plus_y: if (ey == null) null else ey.amount,
     error_minus_y: if (ey == null) null else ey.amount,
     error_plus_x: if (ex == null) null else ex.amount,
     error_minus_x: if (ex == null) null else ex.amount}
}

// Table rows as plot points; non-numeric cells stay sources for symbolic coordinates.
pub fn table_points(plot, base_uri) any^ {
    let table = table_options(plot)
    let options_node = if (table == null) <table_options> else table
    let checked = opts.check(options_node, TABLE_KEYS)^
    let separator = opts.value(options_node, "col sep", "space")
    let valid_separator = if (separator != "space" and separator != "comma" and
        separator != "semicolon" and separator != "tab")
        raise error("unsupported PGFPlots table col sep: " ++ separator) else true
    let row_separator = opts.value(options_node, "row sep", "newline")
    let valid_rows = if (row_separator != "newline" and row_separator != "\\\\")
        raise error("unsupported PGFPlots table row sep: " ++ row_separator) else true
    let text = table_text(plot, base_uri)^
    let rows = [for (row in data_rows(text, row_separator)) cells(row, separator)]
    let valid_data = if (len(rows) == 0) raise error("PGFPlots table is empty") else true
    let header_mode = opts.value(options_node, "header", "true")
    // PGFPlots treats a first row with a non-numeric cell as column names.
    let has_header = header_mode == "has colnames" or (header_mode == "true" and
        any([for (cell in rows[0]) numeric(cell) == null]))
    let width = len(rows[0])
    let header = if (has_header) rows[0] else [for (index in 0 to (width - 1)) string(index)]
    let body = if (has_header) slice(rows, 1, len(rows)) else rows
    let x_column = column_index(header, opts.value(options_node, "x", null),
        opts.value(options_node, "x index", null), 0, "x")^
    let y_column = column_index(header, opts.value(options_node, "y", null),
        opts.value(options_node, "y index", null), 1, "y")^
    let y_error = column_index(header, opts.value(options_node, "y error", null),
        opts.value(options_node, "y error index", null), null, "y error")^
    let x_error = column_index(header, opts.value(options_node, "x error", null),
        opts.value(options_node, "x error index", null), null, "x error")^
    let valid_width = if (width < 2 and opts.value(options_node, "y", null) == null and
        opts.value(options_node, "y index", null) == null)
        raise error("PGFPlots table needs two columns") else true
    let columns = {x: x_column, y: y_column, x_error: x_error, y_error: y_error}
    let points = [for (row in body) table_point(row, columns)]
    let incomplete = [for (point in points where (point.x == null and point.x_source == null) or
        (point.y == null and point.y_source == null)) point]
    if (len(incomplete) > 0) raise error("PGFPlots table row is missing a selected column")
    else points
}

// `symbolic x coords={a,b,c}` as an ordered list, or null.
pub fn symbols(axis_node, axis) {
    let raw = if (axis_node == null) null
        else opts.value(axis_node, "symbolic " ++ axis ++ " coords", null)
    if (raw == null) null
    else [for (part in util.split_top_level(raw, ",") where trim(part) != "") trim(part)]
}

// One coordinate component: a number, or a symbol mapped to its index.
pub fn component(value, source, symbol_list, axis) any^ {
    if (symbol_list != null) {
        // Numeric-looking symbols such as years parse as numbers; match their spelling.
        let key = if (source != null) trim(source)
            else if (value == null) null
            else if (float(int(value)) == float(value)) string(int(value))
            else string(value)
        let matches = if (key == null) [] else [for (index, label in symbol_list
            where label == key) index]
        if (key == null) raise error("symbolic " ++ axis ++ " coords need symbolic values")
        else if (len(matches) == 0)
            raise error("unknown symbolic " ++ axis ++ " coordinate: " ++ key)
        else float(matches[0])
    }
    else if (source != null) {
        let parsed = numeric(source)
        if (parsed == null)
            raise error("non-numeric PGFPlots coordinate needs symbolic " ++ axis ++
                " coords: " ++ source)
        else parsed
    }
    else float(value)
}

// Explicit error offsets of a point; zero when absent.
pub fn errors(point) => {
    plus_x: if (point.error_plus_x == null) 0.0 else abs(float(point.error_plus_x)),
    minus_x: if (point.error_minus_x == null) 0.0 else abs(float(point.error_minus_x)),
    plus_y: if (point.error_plus_y == null) 0.0 else abs(float(point.error_plus_y)),
    minus_y: if (point.error_minus_y == null) 0.0 else abs(float(point.error_minus_y))}
