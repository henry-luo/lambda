// AWFY Benchmark: Json
// Hand-written JSON parser benchmark
// Ported from JavaScript AWFY suite
// Result: PASS when parsed operations array has 156 elements
// NOTE: "" is null in pn mode. Use "~" as EOF sentinel.
// NOTE: cbf flag tracks capture buffer state (0=empty, 1=has content)

// --- Vector (chunked, 16 elements per chunk, up to 256 capacity) ---

pn vec_new() {
    var v = { chunks: [null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null], sz: 0 }
    return v
}

pn vec_add(v, item) {
    var s = (v.sz)
    var ii = s % 16
    var ci = shr(s, 4)
    var cks = (v.chunks)
    var ck = cks[ci]
    if (ck == null) {
        var _n = 0
        ck = [null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null]
        cks[ci] = ck
    }
    var _d = 0
    ck[ii] = item
    var ns = s + 1
    v.sz = ns
}

pn vec_at(v, idx) {
    var ii = idx % 16
    var ci = shr(idx, 4)
    var cks = (v.chunks)
    var ck = cks[ci]
    var r = ck[ii]
    return r
}

pn vec_size(v) {
    var r = (v.sz)
    return r
}

// --- Hash Index Table ---

pn string_hash(s) {
    var n = len(s)
    var h = n * 1402589
    return h
}

pn hit_new() {
    var ht = { tbl: [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0] }
    return ht
}

pn hit_add(ht, name, index) {
    var n = len(name)
    var conv = { v: 0 }
    conv.v = n
    var ni = (conv.v)
    var slot = ni % 32
    var tbl = (ht.tbl)
    if (index < 255) {
        var val = (index + 1) % 256
        var _d1 = 0
        tbl[slot] = val
    }
    if (index >= 255) {
        var _d2 = 0
        tbl[slot] = 0
    }
}

pn hit_get(ht, name) {
    var n = len(name)
    var conv = { v: 0 }
    conv.v = n
    var ni = (conv.v)
    var slot = ni % 32
    var tbl = (ht.tbl)
    var v = tbl[slot]
    var r = v - 1
    return r
}

// --- JSON Value Types ---
// jt=0: null, jt=1: true, jt=2: false
// jt=3: number {jt:3, sv:"..."}
// jt=4: string {jt:4, sv:"..."}
// jt=5: array  {jt:5, vals: vec}
// jt=6: object {jt:6, names: vec, vals: vec, ht: hit}

pn jv_null() {
    var v = { jt: 0 }
    return v
}

pn jv_true() {
    var v = { jt: 1 }
    return v
}

pn jv_false() {
    var v = { jt: 2 }
    return v
}

pn jv_number(s) {
    var v = { jt: 3, sv: s }
    return v
}

pn jv_string(s) {
    var v = { jt: 4, sv: s }
    return v
}

pn jv_array() {
    var vals = vec_new()
    var v = { jt: 5, vals: null }
    v.vals = vals
    return v
}

pn jv_object() {
    var names = vec_new()
    var vals = vec_new()
    var ht = hit_new()
    var v = { jt: 6, names: null, vals: null, ht: null }
    v.names = names
    v.vals = vals
    v.ht = ht
    return v
}

pn jv_arr_add(arr, item) {
    var vals = (arr.vals)
    vec_add(vals, item)
}

pn jv_arr_size(arr) {
    var vals = (arr.vals)
    var r = vec_size(vals)
    return r
}

pn jv_obj_add(obj, name, value) {
    var names = (obj.names)
    var vals = (obj.vals)
    var ht = (obj.ht)
    var idx = vec_size(names)
    hit_add(ht, name, idx)
    vec_add(names, name)
    vec_add(vals, value)
}

pn jv_obj_get(obj, name) {
    var ht = (obj.ht)
    var idx = hit_get(ht, name)
    if (idx == -1) {
        return null
    }
    var names = (obj.names)
    var n = vec_at(names, idx)
    if (n == name) {
        var vals = (obj.vals)
        var r = vec_at(vals, idx)
        return r
    }
    return null
}

pn jv_is_object(v) {
    var jt = (v.jt)
    if (jt == 6) {
        return 1
    }
    return 0
}

pn jv_is_array(v) {
    var jt = (v.jt)
    if (jt == 5) {
        return 1
    }
    return 0
}

// --- Parser ---
// Parser state map. "~" is EOF sentinel (since "" is null in pn mode).
// cbf: capture buffer flag (0=empty, 1=has accumulated content in cb)

pn p_new(input) {
    var p = { inp: input, idx: 0, ln: 1, col: 0, cs: 0, cb: "_", cbf: 0, cur: "~" }
    p.idx = -1
    p.cs = -1
    return p
}

pn p_read(p) {
    var cur = (p.cur)
    if (cur == "\n") {
        var ln = (p.ln) + 1
        p.ln = ln
        p.col = 0
    }
    var idx = (p.idx) + 1
    p.idx = idx
    var inp = (p.inp)
    var inplen = len(inp)
    if (idx < inplen) {
        var ch = inp[idx]
        p.cur = ch
    }
    if (idx >= inplen) {
        var _d = 0
        p.cur = "~"
    }
}

pn p_read_char(p, ch) {
    var cur = (p.cur)
    if (cur != ch) {
        return 0
    }
    p_read(p)
    return 1
}

pn p_start_capture(p) {
    var idx = (p.idx)
    p.cs = idx
}

pn p_pause_capture(p) {
    var idx = (p.idx)
    var end = idx - 1
    var inp = (p.inp)
    var cs = (p.cs)
    var sub = slice(inp, cs, end + 1)
    var cbf = (p.cbf)
    if (cbf == 0) {
        var _d = 0
        p.cb = sub
        p.cbf = 1
    }
    if (cbf != 0) {
        var cb = (p.cb)
        var nc = cb ++ sub
        p.cb = nc
    }
    p.cs = -1
}

pn p_end_capture(p) {
    var cur = (p.cur)
    var idx = (p.idx)
    var end = idx
    if (cur != "~") {
        end = idx - 1
    }
    var inp = (p.inp)
    var cs = (p.cs)
    var sub = slice(inp, cs, end + 1)
    var cbf = (p.cbf)
    var captured = sub
    if (cbf != 0) {
        var cb = (p.cb)
        captured = cb ++ sub
        p.cbf = 0
    }
    p.cs = -1
    return captured
}

pn p_is_ws(p) {
    var cur = (p.cur)
    if (cur == " ") {
        return 1
    }
    if (cur == "\t") {
        return 1
    }
    if (cur == "\n") {
        return 1
    }
    if (cur == "\r") {
        return 1
    }
    return 0
}

pn p_skip_ws(p) {
    var ws = p_is_ws(p)
    while (ws == 1) {
        p_read(p)
        ws = p_is_ws(p)
    }
}

pn p_is_digit(p) {
    var cur = (p.cur)
    if (cur == "0") { return 1 }
    if (cur == "1") { return 1 }
    if (cur == "2") { return 1 }
    if (cur == "3") { return 1 }
    if (cur == "4") { return 1 }
    if (cur == "5") { return 1 }
    if (cur == "6") { return 1 }
    if (cur == "7") { return 1 }
    if (cur == "8") { return 1 }
    if (cur == "9") { return 1 }
    return 0
}

pn p_read_digit(p) {
    var d = p_is_digit(p)
    if (d == 0) {
        return 0
    }
    p_read(p)
    return 1
}

pn p_is_end(p) {
    var cur = (p.cur)
    if (cur == "~") {
        return 1
    }
    return 0
}

// --- Read functions ---

pn p_read_null(p) {
    p_read(p)
    p_read(p)
    p_read(p)
    p_read(p)
    return jv_null()
}

pn p_read_true(p) {
    p_read(p)
    p_read(p)
    p_read(p)
    p_read(p)
    return jv_true()
}

pn p_read_false(p) {
    p_read(p)
    p_read(p)
    p_read(p)
    p_read(p)
    p_read(p)
    return jv_false()
}

pn p_read_escape(p) {
    p_read(p)
    var cur = (p.cur)
    var cbf = (p.cbf)
    var cb = (p.cb)
    if (cbf == 0) {
        cb = "~"
    }
    // build escape char and append to cb
    if (cur == "\"") {
        var nc = cb ++ "\""
        if (cbf == 0) { nc = "\"" }
        var _d1 = 0
        p.cb = nc
        p.cbf = 1
    }
    if (cur == "/") {
        var nc2 = cb ++ "/"
        if (cbf == 0) { nc2 = "/" }
        var _d2 = 0
        p.cb = nc2
        p.cbf = 1
    }
    if (cur == "\\") {
        var nc3 = cb ++ "\\"
        if (cbf == 0) { nc3 = "\\" }
        var _d3 = 0
        p.cb = nc3
        p.cbf = 1
    }
    if (cur == "n") {
        var nc4 = cb ++ "\n"
        if (cbf == 0) { nc4 = "\n" }
        var _d4 = 0
        p.cb = nc4
        p.cbf = 1
    }
    if (cur == "r") {
        var nc5 = cb ++ "\r"
        if (cbf == 0) { nc5 = "\r" }
        var _d5 = 0
        p.cb = nc5
        p.cbf = 1
    }
    if (cur == "t") {
        var nc6 = cb ++ "\t"
        if (cbf == 0) { nc6 = "\t" }
        var _d6 = 0
        p.cb = nc6
        p.cbf = 1
    }
    p_read(p)
}

pn p_read_string_internal(p) {
    p_read(p)
    p_start_capture(p)
    var cur = (p.cur)
    while (cur != "\"") {
        if (cur == "\\") {
            p_pause_capture(p)
            p_read_escape(p)
            p_start_capture(p)
        }
        if (cur != "\\") {
            p_read(p)
        }
        cur = (p.cur)
    }
    var result = p_end_capture(p)
    p_read(p)
    return result
}

pn p_read_string(p) {
    var s = p_read_string_internal(p)
    var r = jv_string(s)
    return r
}

pn p_read_fraction(p) {
    var rc = p_read_char(p, ".")
    if (rc == 0) {
        return 0
    }
    var rd = p_read_digit(p)
    var cont = p_read_digit(p)
    while (cont == 1) {
        cont = p_read_digit(p)
    }
    return 1
}

pn p_read_exponent(p) {
    var rc1 = p_read_char(p, "e")
    if (rc1 == 0) {
        var rc2 = p_read_char(p, "E")
        if (rc2 == 0) {
            return 0
        }
    }
    var rp = p_read_char(p, "+")
    if (rp == 0) {
        var rm = p_read_char(p, "-")
        var _d = rm
    }
    var rd = p_read_digit(p)
    var cont = p_read_digit(p)
    while (cont == 1) {
        cont = p_read_digit(p)
    }
    return 1
}

pn p_read_number(p) {
    p_start_capture(p)
    var rm = p_read_char(p, "-")
    var cur = (p.cur)
    var rd = p_read_digit(p)
    if (cur != "0") {
        var more = p_read_digit(p)
        while (more == 1) {
            more = p_read_digit(p)
        }
    }
    p_read_fraction(p)
    p_read_exponent(p)
    var s = p_end_capture(p)
    var r = jv_number(s)
    return r
}

pn p_read_array(p) {
    p_read(p)
    var arr = jv_array()
    p_skip_ws(p)
    var rc = p_read_char(p, "]")
    if (rc == 1) {
        return arr
    }
    var cont = 1
    while (cont == 1) {
        p_skip_ws(p)
        var val = p_read_value(p)
        jv_arr_add(arr, val)
        p_skip_ws(p)
        cont = p_read_char(p, ",")
    }
    var rc2 = p_read_char(p, "]")
    return arr
}

pn p_read_object(p) {
    p_read(p)
    var obj = jv_object()
    p_skip_ws(p)
    var rc = p_read_char(p, "}")
    if (rc == 1) {
        return obj
    }
    var cont = 1
    while (cont == 1) {
        p_skip_ws(p)
        var name = p_read_string_internal(p)
        p_skip_ws(p)
        var rc2 = p_read_char(p, ":")
        p_skip_ws(p)
        var val = p_read_value(p)
        jv_obj_add(obj, name, val)
        p_skip_ws(p)
        cont = p_read_char(p, ",")
    }
    var rc3 = p_read_char(p, "}")
    return obj
}

pn p_read_value(p) {
    var cur = (p.cur)
    if (cur == "n") {
        return p_read_null(p)
    }
    if (cur == "t") {
        return p_read_true(p)
    }
    if (cur == "f") {
        return p_read_false(p)
    }
    if (cur == "\"") {
        return p_read_string(p)
    }
    if (cur == "[") {
        return p_read_array(p)
    }
    if (cur == "{") {
        return p_read_object(p)
    }
    // must be a number
    return p_read_number(p)
}

pn parse_json(input) {
    var p = p_new(input)
    p_read(p)
    p_skip_ws(p)
    var result = p_read_value(p)
    return result
}

// --- Benchmark data ---
let RAP = "{\"head\":{\"requestCounter\":4},\"operations\":[[\"destroy\",\"w54\"],[\"set\",\"w2\",{\"activeControl\":\"w99\"}],[\"set\",\"w21\",{\"customVariant\":\"variant_navigation\"}],[\"set\",\"w28\",{\"customVariant\":\"variant_selected\"}],[\"set\",\"w53\",{\"children\":[\"w95\"]}],[\"create\",\"w95\",\"rwt.widgets.Composite\",{\"parent\":\"w53\",\"style\":[\"NONE\"],\"bounds\":[0,0,1008,586],\"children\":[\"w96\",\"w97\"],\"tabIndex\":-1,\"clientArea\":[0,0,1008,586]}],[\"create\",\"w96\",\"rwt.widgets.Label\",{\"parent\":\"w95\",\"style\":[\"NONE\"],\"bounds\":[10,30,112,26],\"tabIndex\":-1,\"customVariant\":\"variant_pageHeadline\",\"text\":\"TableViewer\"}],[\"create\",\"w97\",\"rwt.widgets.Composite\",{\"parent\":\"w95\",\"style\":[\"NONE\"],\"bounds\":[0,61,1008,525],\"children\":[\"w98\",\"w99\",\"w226\",\"w228\"],\"tabIndex\":-1,\"clientArea\":[0,0,1008,525]}],[\"create\",\"w98\",\"rwt.widgets.Text\",{\"parent\":\"w97\",\"style\":[\"LEFT\",\"SINGLE\",\"BORDER\"],\"bounds\":[10,10,988,32],\"tabIndex\":22,\"activeKeys\":[\"#13\",\"#27\",\"#40\"]}],[\"listen\",\"w98\",{\"KeyDown\":true,\"Modify\":true}],[\"create\",\"w99\",\"rwt.widgets.Grid\",{\"parent\":\"w97\",\"style\":[\"SINGLE\",\"BORDER\"],\"appearance\":\"table\",\"indentionWidth\":0,\"treeColumn\":-1,\"markupEnabled\":false}],[\"create\",\"w100\",\"rwt.widgets.ScrollBar\",{\"parent\":\"w99\",\"style\":[\"HORIZONTAL\"]}],[\"create\",\"w101\",\"rwt.widgets.ScrollBar\",{\"parent\":\"w99\",\"style\":[\"VERTICAL\"]}],[\"set\",\"w99\",{\"bounds\":[10,52,988,402],\"children\":[],\"tabIndex\":23,\"activeKeys\":[\"CTRL+#70\",\"CTRL+#78\",\"CTRL+#82\",\"CTRL+#89\",\"CTRL+#83\",\"CTRL+#71\",\"CTRL+#69\"],\"cancelKeys\":[\"CTRL+#70\",\"CTRL+#78\",\"CTRL+#82\",\"CTRL+#89\",\"CTRL+#83\",\"CTRL+#71\",\"CTRL+#69\"]}],[\"listen\",\"w99\",{\"MouseDown\":true,\"MouseUp\":true,\"MouseDoubleClick\":true,\"KeyDown\":true}],[\"set\",\"w99\",{\"itemCount\":118,\"itemHeight\":28,\"itemMetrics\":[[0,0,50,3,0,3,44],[1,50,50,53,0,53,44],[2,100,140,103,0,103,134],[3,240,180,243,0,243,174],[4,420,50,423,0,423,44],[5,470,50,473,0,473,44]],\"columnCount\":6,\"headerHeight\":35,\"headerVisible\":true,\"linesVisible\":true,\"focusItem\":\"w108\",\"selection\":[\"w108\"]}],[\"listen\",\"w99\",{\"Selection\":true,\"DefaultSelection\":true}],[\"set\",\"w99\",{\"enableCellToolTip\":true}],[\"listen\",\"w100\",{\"Selection\":true}],[\"set\",\"w101\",{\"visibility\":true}],[\"listen\",\"w101\",{\"Selection\":true}],[\"create\",\"w102\",\"rwt.widgets.GridColumn\",{\"parent\":\"w99\",\"text\":\"Nr.\",\"width\":50,\"moveable\":true}],[\"listen\",\"w102\",{\"Selection\":true}],[\"create\",\"w103\",\"rwt.widgets.GridColumn\",{\"parent\":\"w99\",\"text\":\"Sym.\",\"index\":1,\"left\":50,\"width\":50,\"moveable\":true}],[\"listen\",\"w103\",{\"Selection\":true}],[\"create\",\"w104\",\"rwt.widgets.GridColumn\",{\"parent\":\"w99\",\"text\":\"Name\",\"index\":2,\"left\":100,\"width\":140,\"moveable\":true}],[\"listen\",\"w104\",{\"Selection\":true}],[\"create\",\"w105\",\"rwt.widgets.GridColumn\",{\"parent\":\"w99\",\"text\":\"Series\",\"index\":3,\"left\":240,\"width\":180,\"moveable\":true}],[\"listen\",\"w105\",{\"Selection\":true}],[\"create\",\"w106\",\"rwt.widgets.GridColumn\",{\"parent\":\"w99\",\"text\":\"Group\",\"index\":4,\"left\":420,\"width\":50,\"moveable\":true}],[\"listen\",\"w106\",{\"Selection\":true}],[\"create\",\"w107\",\"rwt.widgets.GridColumn\",{\"parent\":\"w99\",\"text\":\"Period\",\"index\":5,\"left\":470,\"width\":50,\"moveable\":true}],[\"listen\",\"w107\",{\"Selection\":true}],[\"create\",\"w108\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":0,\"texts\":[\"1\",\"H\",\"Hydrogen\",\"Nonmetal\",\"1\",\"1\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w109\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":1,\"texts\":[\"2\",\"He\",\"Helium\",\"Noble gas\",\"18\",\"1\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w110\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":2,\"texts\":[\"3\",\"Li\",\"Lithium\",\"Alkali metal\",\"1\",\"2\"],\"cellBackgrounds\":[null,null,null,[239,41,41,255],null,null]}],[\"create\",\"w111\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":3,\"texts\":[\"4\",\"Be\",\"Beryllium\",\"Alkaline earth metal\",\"2\",\"2\"],\"cellBackgrounds\":[null,null,null,[233,185,110,255],null,null]}],[\"create\",\"w112\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":4,\"texts\":[\"5\",\"B\",\"Boron\",\"Metalloid\",\"13\",\"2\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w113\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":5,\"texts\":[\"6\",\"C\",\"Carbon\",\"Nonmetal\",\"14\",\"2\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w114\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":6,\"texts\":[\"7\",\"N\",\"Nitrogen\",\"Nonmetal\",\"15\",\"2\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w115\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":7,\"texts\":[\"8\",\"O\",\"Oxygen\",\"Nonmetal\",\"16\",\"2\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w116\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":8,\"texts\":[\"9\",\"F\",\"Fluorine\",\"Halogen\",\"17\",\"2\"],\"cellBackgrounds\":[null,null,null,[252,233,79,255],null,null]}],[\"create\",\"w117\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":9,\"texts\":[\"10\",\"Ne\",\"Neon\",\"Noble gas\",\"18\",\"2\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w118\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":10,\"texts\":[\"11\",\"Na\",\"Sodium\",\"Alkali metal\",\"1\",\"3\"],\"cellBackgrounds\":[null,null,null,[239,41,41,255],null,null]}],[\"create\",\"w119\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":11,\"texts\":[\"12\",\"Mg\",\"Magnesium\",\"Alkaline earth metal\",\"2\",\"3\"],\"cellBackgrounds\":[null,null,null,[233,185,110,255],null,null]}],[\"create\",\"w120\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":12,\"texts\":[\"13\",\"Al\",\"Aluminium\",\"Poor metal\",\"13\",\"3\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w121\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":13,\"texts\":[\"14\",\"Si\",\"Silicon\",\"Metalloid\",\"14\",\"3\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w122\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":14,\"texts\":[\"15\",\"P\",\"Phosphorus\",\"Nonmetal\",\"15\",\"3\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w123\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":15,\"texts\":[\"16\",\"S\",\"Sulfur\",\"Nonmetal\",\"16\",\"3\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w124\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":16,\"texts\":[\"17\",\"Cl\",\"Chlorine\",\"Halogen\",\"17\",\"3\"],\"cellBackgrounds\":[null,null,null,[252,233,79,255],null,null]}],[\"create\",\"w125\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":17,\"texts\":[\"18\",\"Ar\",\"Argon\",\"Noble gas\",\"18\",\"3\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w126\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":18,\"texts\":[\"19\",\"K\",\"Potassium\",\"Alkali metal\",\"1\",\"4\"],\"cellBackgrounds\":[null,null,null,[239,41,41,255],null,null]}],[\"create\",\"w127\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":19,\"texts\":[\"20\",\"Ca\",\"Calcium\",\"Alkaline earth metal\",\"2\",\"4\"],\"cellBackgrounds\":[null,null,null,[233,185,110,255],null,null]}],[\"create\",\"w128\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":20,\"texts\":[\"21\",\"Sc\",\"Scandium\",\"Transition metal\",\"3\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w129\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":21,\"texts\":[\"22\",\"Ti\",\"Titanium\",\"Transition metal\",\"4\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w130\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":22,\"texts\":[\"23\",\"V\",\"Vanadium\",\"Transition metal\",\"5\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w131\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":23,\"texts\":[\"24\",\"Cr\",\"Chromium\",\"Transition metal\",\"6\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w132\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":24,\"texts\":[\"25\",\"Mn\",\"Manganese\",\"Transition metal\",\"7\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w133\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":25,\"texts\":[\"26\",\"Fe\",\"Iron\",\"Transition metal\",\"8\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w134\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":26,\"texts\":[\"27\",\"Co\",\"Cobalt\",\"Transition metal\",\"9\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w135\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":27,\"texts\":[\"28\",\"Ni\",\"Nickel\",\"Transition metal\",\"10\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w136\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":28,\"texts\":[\"29\",\"Cu\",\"Copper\",\"Transition metal\",\"11\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w137\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":29,\"texts\":[\"30\",\"Zn\",\"Zinc\",\"Transition metal\",\"12\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w138\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":30,\"texts\":[\"31\",\"Ga\",\"Gallium\",\"Poor metal\",\"13\",\"4\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w139\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":31,\"texts\":[\"32\",\"Ge\",\"Germanium\",\"Metalloid\",\"14\",\"4\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w140\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":32,\"texts\":[\"33\",\"As\",\"Arsenic\",\"Metalloid\",\"15\",\"4\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w141\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":33,\"texts\":[\"34\",\"Se\",\"Selenium\",\"Nonmetal\",\"16\",\"4\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w142\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":34,\"texts\":[\"35\",\"Br\",\"Bromine\",\"Halogen\",\"17\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,233,79,255],null,null]}],[\"create\",\"w143\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":35,\"texts\":[\"36\",\"Kr\",\"Krypton\",\"Noble gas\",\"18\",\"4\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w144\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":36,\"texts\":[\"37\",\"Rb\",\"Rubidium\",\"Alkali metal\",\"1\",\"5\"],\"cellBackgrounds\":[null,null,null,[239,41,41,255],null,null]}],[\"create\",\"w145\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":37,\"texts\":[\"38\",\"Sr\",\"Strontium\",\"Alkaline earth metal\",\"2\",\"5\"],\"cellBackgrounds\":[null,null,null,[233,185,110,255],null,null]}],[\"create\",\"w146\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":38,\"texts\":[\"39\",\"Y\",\"Yttrium\",\"Transition metal\",\"3\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w147\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":39,\"texts\":[\"40\",\"Zr\",\"Zirconium\",\"Transition metal\",\"4\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w148\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":40,\"texts\":[\"41\",\"Nb\",\"Niobium\",\"Transition metal\",\"5\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w149\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":41,\"texts\":[\"42\",\"Mo\",\"Molybdenum\",\"Transition metal\",\"6\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w150\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":42,\"texts\":[\"43\",\"Tc\",\"Technetium\",\"Transition metal\",\"7\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w151\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":43,\"texts\":[\"44\",\"Ru\",\"Ruthenium\",\"Transition metal\",\"8\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w152\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":44,\"texts\":[\"45\",\"Rh\",\"Rhodium\",\"Transition metal\",\"9\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w153\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":45,\"texts\":[\"46\",\"Pd\",\"Palladium\",\"Transition metal\",\"10\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w154\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":46,\"texts\":[\"47\",\"Ag\",\"Silver\",\"Transition metal\",\"11\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w155\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":47,\"texts\":[\"48\",\"Cd\",\"Cadmium\",\"Transition metal\",\"12\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w156\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":48,\"texts\":[\"49\",\"In\",\"Indium\",\"Poor metal\",\"13\",\"5\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w157\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":49,\"texts\":[\"50\",\"Sn\",\"Tin\",\"Poor metal\",\"14\",\"5\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w158\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":50,\"texts\":[\"51\",\"Sb\",\"Antimony\",\"Metalloid\",\"15\",\"5\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w159\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":51,\"texts\":[\"52\",\"Te\",\"Tellurium\",\"Metalloid\",\"16\",\"5\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w160\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":52,\"texts\":[\"53\",\"I\",\"Iodine\",\"Halogen\",\"17\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,233,79,255],null,null]}],[\"create\",\"w161\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":53,\"texts\":[\"54\",\"Xe\",\"Xenon\",\"Noble gas\",\"18\",\"5\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w162\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":54,\"texts\":[\"55\",\"Cs\",\"Caesium\",\"Alkali metal\",\"1\",\"6\"],\"cellBackgrounds\":[null,null,null,[239,41,41,255],null,null]}],[\"create\",\"w163\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":55,\"texts\":[\"56\",\"Ba\",\"Barium\",\"Alkaline earth metal\",\"2\",\"6\"],\"cellBackgrounds\":[null,null,null,[233,185,110,255],null,null]}],[\"create\",\"w164\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":56,\"texts\":[\"57\",\"La\",\"Lanthanum\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w165\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":57,\"texts\":[\"58\",\"Ce\",\"Cerium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w166\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":58,\"texts\":[\"59\",\"Pr\",\"Praseodymium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w167\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":59,\"texts\":[\"60\",\"Nd\",\"Neodymium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w168\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":60,\"texts\":[\"61\",\"Pm\",\"Promethium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w169\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":61,\"texts\":[\"62\",\"Sm\",\"Samarium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w170\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":62,\"texts\":[\"63\",\"Eu\",\"Europium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w171\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":63,\"texts\":[\"64\",\"Gd\",\"Gadolinium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w172\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":64,\"texts\":[\"65\",\"Tb\",\"Terbium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w173\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":65,\"texts\":[\"66\",\"Dy\",\"Dysprosium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w174\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":66,\"texts\":[\"67\",\"Ho\",\"Holmium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w175\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":67,\"texts\":[\"68\",\"Er\",\"Erbium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w176\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":68,\"texts\":[\"69\",\"Tm\",\"Thulium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w177\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":69,\"texts\":[\"70\",\"Yb\",\"Ytterbium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w178\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":70,\"texts\":[\"71\",\"Lu\",\"Lutetium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w179\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":71,\"texts\":[\"72\",\"Hf\",\"Hafnium\",\"Transition metal\",\"4\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w180\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":72,\"texts\":[\"73\",\"Ta\",\"Tantalum\",\"Transition metal\",\"5\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w181\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":73,\"texts\":[\"74\",\"W\",\"Tungsten\",\"Transition metal\",\"6\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w182\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":74,\"texts\":[\"75\",\"Re\",\"Rhenium\",\"Transition metal\",\"7\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w183\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":75,\"texts\":[\"76\",\"Os\",\"Osmium\",\"Transition metal\",\"8\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w184\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":76,\"texts\":[\"77\",\"Ir\",\"Iridium\",\"Transition metal\",\"9\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w185\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":77,\"texts\":[\"78\",\"Pt\",\"Platinum\",\"Transition metal\",\"10\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w186\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":78,\"texts\":[\"79\",\"Au\",\"Gold\",\"Transition metal\",\"11\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w187\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":79,\"texts\":[\"80\",\"Hg\",\"Mercury\",\"Transition metal\",\"12\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w188\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":80,\"texts\":[\"81\",\"Tl\",\"Thallium\",\"Poor metal\",\"13\",\"6\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w189\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":81,\"texts\":[\"82\",\"Pb\",\"Lead\",\"Poor metal\",\"14\",\"6\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w190\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":82,\"texts\":[\"83\",\"Bi\",\"Bismuth\",\"Poor metal\",\"15\",\"6\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w191\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":83,\"texts\":[\"84\",\"Po\",\"Polonium\",\"Metalloid\",\"16\",\"6\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w192\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":84,\"texts\":[\"85\",\"At\",\"Astatine\",\"Halogen\",\"17\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,233,79,255],null,null]}],[\"create\",\"w193\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":85,\"texts\":[\"86\",\"Rn\",\"Radon\",\"Noble gas\",\"18\",\"6\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w194\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":86,\"texts\":[\"87\",\"Fr\",\"Francium\",\"Alkali metal\",\"1\",\"7\"],\"cellBackgrounds\":[null,null,null,[239,41,41,255],null,null]}],[\"create\",\"w195\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":87,\"texts\":[\"88\",\"Ra\",\"Radium\",\"Alkaline earth metal\",\"2\",\"7\"],\"cellBackgrounds\":[null,null,null,[233,185,110,255],null,null]}],[\"create\",\"w196\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":88,\"texts\":[\"89\",\"Ac\",\"Actinium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w197\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":89,\"texts\":[\"90\",\"Th\",\"Thorium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w198\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":90,\"texts\":[\"91\",\"Pa\",\"Protactinium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w199\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":91,\"texts\":[\"92\",\"U\",\"Uranium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w200\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":92,\"texts\":[\"93\",\"Np\",\"Neptunium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w201\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":93,\"texts\":[\"94\",\"Pu\",\"Plutonium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w202\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":94,\"texts\":[\"95\",\"Am\",\"Americium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w203\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":95,\"texts\":[\"96\",\"Cm\",\"Curium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w204\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":96,\"texts\":[\"97\",\"Bk\",\"Berkelium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w205\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":97,\"texts\":[\"98\",\"Cf\",\"Californium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w206\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":98,\"texts\":[\"99\",\"Es\",\"Einsteinium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w207\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":99,\"texts\":[\"100\",\"Fm\",\"Fermium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w208\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":100,\"texts\":[\"101\",\"Md\",\"Mendelevium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w209\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":101,\"texts\":[\"102\",\"No\",\"Nobelium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w210\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":102,\"texts\":[\"103\",\"Lr\",\"Lawrencium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w211\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":103,\"texts\":[\"104\",\"Rf\",\"Rutherfordium\",\"Transition metal\",\"4\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w212\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":104,\"texts\":[\"105\",\"Db\",\"Dubnium\",\"Transition metal\",\"5\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w213\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":105,\"texts\":[\"106\",\"Sg\",\"Seaborgium\",\"Transition metal\",\"6\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w214\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":106,\"texts\":[\"107\",\"Bh\",\"Bohrium\",\"Transition metal\",\"7\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w215\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":107,\"texts\":[\"108\",\"Hs\",\"Hassium\",\"Transition metal\",\"8\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w216\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":108,\"texts\":[\"109\",\"Mt\",\"Meitnerium\",\"Transition metal\",\"9\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w217\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":109,\"texts\":[\"110\",\"Ds\",\"Darmstadtium\",\"Transition metal\",\"10\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w218\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":110,\"texts\":[\"111\",\"Rg\",\"Roentgenium\",\"Transition metal\",\"11\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w219\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":111,\"texts\":[\"112\",\"Uub\",\"Ununbium\",\"Transition metal\",\"12\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w220\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":112,\"texts\":[\"113\",\"Uut\",\"Ununtrium\",\"Poor metal\",\"13\",\"7\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w221\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":113,\"texts\":[\"114\",\"Uuq\",\"Ununquadium\",\"Poor metal\",\"14\",\"7\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w222\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":114,\"texts\":[\"115\",\"Uup\",\"Ununpentium\",\"Poor metal\",\"15\",\"7\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w223\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":115,\"texts\":[\"116\",\"Uuh\",\"Ununhexium\",\"Poor metal\",\"16\",\"7\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w224\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":116,\"texts\":[\"117\",\"Uus\",\"Ununseptium\",\"Halogen\",\"17\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,233,79,255],null,null]}],[\"create\",\"w225\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":117,\"texts\":[\"118\",\"Uuo\",\"Ununoctium\",\"Noble gas\",\"18\",\"7\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w226\",\"rwt.widgets.Composite\",{\"parent\":\"w97\",\"style\":[\"BORDER\"],\"bounds\":[10,464,988,25],\"children\":[\"w227\"],\"tabIndex\":-1,\"clientArea\":[0,0,986,23]}],[\"create\",\"w227\",\"rwt.widgets.Label\",{\"parent\":\"w226\",\"style\":[\"NONE\"],\"bounds\":[10,10,966,3],\"tabIndex\":-1,\"text\":\"Hydrogen (H)\"}],[\"create\",\"w228\",\"rwt.widgets.Label\",{\"parent\":\"w97\",\"style\":[\"WRAP\"],\"bounds\":[10,499,988,16],\"tabIndex\":-1,\"foreground\":[150,150,150,255],\"font\":[[\"Verdana\",\"Lucida Sans\",\"Arial\",\"Helvetica\",\"sans-serif\"],10,false,false],\"text\":\"Shortcuts: [CTRL+F] - Filter | Sort by: [CTRL+R] - Number, [CTRL+Y] - Symbol, [CTRL+N] - Name, [CTRL+S] - Series, [CTRL+G] - Group, [CTRL+E] - Period\"}],[\"set\",\"w1\",{\"focusControl\":\"w99\"}],[\"call\",\"rwt.client.BrowserNavigation\",\"addToHistory\",{\"entries\":[[\"tableviewer\",\"TableViewer\"]]}]]}"

pn benchmark() {
    var result = parse_json(RAP)
    var isobj = jv_is_object(result)
    if (isobj != 1) {
        print("FAIL: not object\n")
        return 0
    }
    var head = jv_obj_get(result, "head")
    if (head == null) {
        print("FAIL: no head\n")
        return 0
    }
    var hobj = jv_is_object(head)
    if (hobj != 1) {
        print("FAIL: head not object\n")
        return 0
    }
    var ops = jv_obj_get(result, "operations")
    if (ops == null) {
        print("FAIL: no operations\n")
        return 0
    }
    var oarr = jv_is_array(ops)
    if (oarr != 1) {
        print("FAIL: operations not array\n")
        return 0
    }
    var opsz = jv_arr_size(ops)
    if (opsz == 156) {
        return 1
    }
    print("FAIL: operations size=")
    print(opsz)
    print("\n")
    return 0
}

pn main() {
    var result = benchmark()
    if (result == 1) {
        print("Json: PASS\n")
    }
    if (result == 0) {
        print("Json: FAIL\n")
    }
}
