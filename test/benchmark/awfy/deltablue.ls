// AWFY Benchmark: DeltaBlue
// Constraint solver benchmark
// Ported from JavaScript AWFY suite

// --- Constants ---
let FORWARD = 1
let BACKWARD = 2
// Strength (arithmetic values, lower = stronger)
let S_ABSOLUTE_STRONGEST = -10000
let S_REQUIRED = -800
let S_STRONG_PREFERRED = -600
let S_PREFERRED = -400
let S_STRONG_DEFAULT = -200
let S_DEFAULT = 0
let S_WEAK_DEFAULT = 500
let S_ABSOLUTE_WEAKEST = 10000
// Constraint kinds
let K_EDIT = 1
let K_STAY = 2
let K_EQUAL = 3
let K_SCALE = 4

// --- Vector (chunked 16x16=256) ---

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

pn vec_set(v, idx, item) {
    var ii = idx % 16
    var ci = shr(idx, 4)
    var cks = (v.chunks)
    var ck = cks[ci]
    var _d = 0
    ck[ii] = item
}

pn vec_is_empty(v) {
    var sz = (v.sz)
    if (sz == 0) { return 1 }
    return 0
}

pn vec_remove_first(v) {
    var sz = (v.sz)
    if (sz == 0) { return null }
    var first = vec_at(v, 0)
    var i = 1
    while (i < sz) {
        var elem = vec_at(v, i)
        var pi = i - 1
        vec_set(v, pi, elem)
        i = i + 1
    }
    var nsz = sz - 1
    v.sz = nsz
    return first
}

pn vec_with(item) {
    var v = vec_new()
    vec_add(v, item)
    return v
}

// Remove constraint by cid from vector
pn vec_remove_cid(v, cid) {
    var sz = (v.sz)
    var found = -1
    var i = 0
    while (i < sz) {
        if (found == -1) {
            var elem = vec_at(v, i)
            var ecid = (elem.cid)
            if (ecid == cid) {
                found = i
            }
        }
        i = i + 1
    }
    if (found == -1) { return 0 }
    var j = found + 1
    while (j < sz) {
        var elem2 = vec_at(v, j)
        var pj = j - 1
        vec_set(v, pj, elem2)
        j = j + 1
    }
    var nsz = sz - 1
    v.sz = nsz
    return 1
}

// --- Strength helpers ---
pn s_stronger(a, b) {
    if (a < b) { return 1 }
    return 0
}

pn s_weaker(a, b) {
    if (a > b) { return 1 }
    return 0
}

pn s_weakest(a, b) {
    if (a > b) { return a }
    return b
}

// --- Variable ---
pn var_new() {
    var cs = vec_new()
    var v = { val: 0, constraints: null, determinedBy: 0, walkStrength: 10000, stay: 1, mark: 0 }
    v.constraints = cs
    return v
}

pn var_value(aValue) {
    var v = var_new()
    v.val = aValue
    return v
}

pn var_add_constraint(variable, c) {
    var cs = (variable.constraints)
    vec_add(cs, c)
}

pn var_remove_constraint(variable, c) {
    var cs = (variable.constraints)
    var ccid = (c.cid)
    vec_remove_cid(cs, ccid)
    var det = (variable.determinedBy)
    if (det == ccid) {
        var _d = 0
        variable.determinedBy = 0
    }
}

// --- Constraint dispatch ---
// All constraints: { cid:INT, kind:INT, strength:INT, ... }
// Unary (Edit/Stay): { cid, kind:1|2, strength, out:var, satisfied:0|1 }
// Binary (Equal): { cid, kind:3, strength, v1:var, v2:var, direction:0|1|2 }
// Scale: { cid, kind:4, strength, v1:var, v2:var, direction:0|1|2, sc:var, off:var }
// determinedBy stores cid (INT), 0 = no constraint

pn c_is_input(c) {
    var k = (c.kind)
    if (k == K_EDIT) { return 1 }
    return 0
}

pn c_is_satisfied(c) {
    var k = (c.kind)
    if (k == K_EDIT) {
        var s = (c.satisfied)
        return s
    }
    if (k == K_STAY) {
        var s2 = (c.satisfied)
        return s2
    }
    // binary: satisfied = direction != 0
    var dir = (c.direction)
    if (dir != 0) { return 1 }
    return 0
}

pn c_add_to_graph(c) {
    var k = (c.kind)
    if (k == K_EDIT) {
        var o = (c.out)
        var_add_constraint(o, c)
        c.satisfied = 0
    }
    if (k == K_STAY) {
        var o2 = (c.out)
        var_add_constraint(o2, c)
        c.satisfied = 0
    }
    if (k == K_EQUAL) {
        var v1 = (c.v1)
        var v2 = (c.v2)
        var_add_constraint(v1, c)
        var_add_constraint(v2, c)
        c.direction = 0
    }
    if (k == K_SCALE) {
        var v1s = (c.v1)
        var v2s = (c.v2)
        var sc = (c.sc)
        var off = (c.off)
        var_add_constraint(v1s, c)
        var_add_constraint(v2s, c)
        var_add_constraint(sc, c)
        var_add_constraint(off, c)
        c.direction = 0
    }
}

pn c_remove_from_graph(c) {
    var k = (c.kind)
    if (k == K_EDIT) {
        var o = (c.out)
        if (o != null) {
            var_remove_constraint(o, c)
        }
        c.satisfied = 0
    }
    if (k == K_STAY) {
        var o2 = (c.out)
        if (o2 != null) {
            var_remove_constraint(o2, c)
        }
        c.satisfied = 0
    }
    if (k == K_EQUAL) {
        var v1 = (c.v1)
        if (v1 != null) { var_remove_constraint(v1, c) }
        var v2 = (c.v2)
        if (v2 != null) { var_remove_constraint(v2, c) }
        c.direction = 0
    }
    if (k == K_SCALE) {
        var v1s = (c.v1)
        if (v1s != null) { var_remove_constraint(v1s, c) }
        var v2s = (c.v2)
        if (v2s != null) { var_remove_constraint(v2s, c) }
        var sc = (c.sc)
        if (sc != null) { var_remove_constraint(sc, c) }
        var off = (c.off)
        if (off != null) { var_remove_constraint(off, c) }
        c.direction = 0
    }
}

pn c_choose_method(c, mark) {
    var k = (c.kind)
    if (k == K_EDIT) {
        var o = (c.out)
        var om = (o.mark)
        var ows = (o.walkStrength)
        var cs = (c.strength)
        if (om != mark) {
            var str = s_stronger(cs, ows)
            if (str == 1) {
                c.satisfied = 1
                return 0
            }
        }
        c.satisfied = 0
        return 0
    }
    if (k == K_STAY) {
        var o2 = (c.out)
        var om2 = (o2.mark)
        var ows2 = (o2.walkStrength)
        var cs2 = (c.strength)
        if (om2 != mark) {
            var str2 = s_stronger(cs2, ows2)
            if (str2 == 1) {
                c.satisfied = 1
                return 0
            }
        }
        c.satisfied = 0
        return 0
    }
    // Binary/Scale
    var v1 = (c.v1)
    var v2 = (c.v2)
    var v1m = (v1.mark)
    var v2m = (v2.mark)
    var v1ws = (v1.walkStrength)
    var v2ws = (v2.walkStrength)
    var cs3 = (c.strength)

    if (v1m == mark) {
        if (v2m != mark) {
            var sf = s_stronger(cs3, v2ws)
            if (sf == 1) {
                c.direction = FORWARD
                return 0
            }
        }
        c.direction = 0
        return 0
    }
    if (v2m == mark) {
        if (v1m != mark) {
            var sb = s_stronger(cs3, v1ws)
            if (sb == 1) {
                c.direction = BACKWARD
                return 0
            }
        }
        c.direction = 0
        return 0
    }
    // Neither marked
    var w1 = s_weaker(v1ws, v2ws)
    if (w1 == 1) {
        var sb2 = s_stronger(cs3, v1ws)
        if (sb2 == 1) {
            c.direction = BACKWARD
            return 0
        }
        c.direction = 0
        return 0
    }
    var sf2 = s_stronger(cs3, v2ws)
    if (sf2 == 1) {
        c.direction = FORWARD
        return 0
    }
    c.direction = 0
    return 0
}

pn c_mark_unsatisfied(c) {
    var k = (c.kind)
    if (k == K_EDIT) {
        var _d = 0
        c.satisfied = 0
    }
    if (k == K_STAY) {
        var _d2 = 0
        c.satisfied = 0
    }
    if (k == K_EQUAL) {
        var _d3 = 0
        c.direction = 0
    }
    if (k == K_SCALE) {
        var _d4 = 0
        c.direction = 0
    }
}

pn c_get_output(c) {
    var k = (c.kind)
    if (k == K_EDIT) {
        var o = (c.out)
        return o
    }
    if (k == K_STAY) {
        var o2 = (c.out)
        return o2
    }
    // binary: forward→v2, backward→v1
    var dir = (c.direction)
    if (dir == FORWARD) {
        var v2 = (c.v2)
        return v2
    }
    var v1 = (c.v1)
    return v1
}

pn c_mark_inputs(c, mark) {
    var k = (c.kind)
    // Unary: no inputs
    if (k == K_EDIT) { return 0 }
    if (k == K_STAY) { return 0 }
    // Binary: input is the non-output variable
    var dir = (c.direction)
    if (k == K_EQUAL) {
        if (dir == FORWARD) {
            var v1 = (c.v1)
            v1.mark = mark
        }
        if (dir == BACKWARD) {
            var v2 = (c.v2)
            v2.mark = mark
        }
    }
    if (k == K_SCALE) {
        if (dir == FORWARD) {
            var sv1 = (c.v1)
            sv1.mark = mark
        }
        if (dir == BACKWARD) {
            var sv2 = (c.v2)
            sv2.mark = mark
        }
        var sc = (c.sc)
        sc.mark = mark
        var off = (c.off)
        off.mark = mark
    }
    return 0
}

// inputsKnown: all inputs must satisfy (mark==mark || stay || determinedBy==0)
pn c_inputs_known(c, mark) {
    var k = (c.kind)
    if (k == K_EDIT) { return 1 }
    if (k == K_STAY) { return 1 }
    var dir = (c.direction)
    // Check input variable
    if (k == K_EQUAL) {
        var inp = null
        if (dir == FORWARD) { inp = (c.v1) }
        if (dir == BACKWARD) { inp = (c.v2) }
        if (inp != null) {
            var im = (inp.mark)
            var ist = (inp.stay)
            var idet = (inp.determinedBy)
            if (im != mark) {
                if (ist == 0) {
                    if (idet != 0) { return 0 }
                }
            }
        }
    }
    if (k == K_SCALE) {
        var sinp = null
        if (dir == FORWARD) { sinp = (c.v1) }
        if (dir == BACKWARD) { sinp = (c.v2) }
        if (sinp != null) {
            var sim = (sinp.mark)
            var sist = (sinp.stay)
            var sidet = (sinp.determinedBy)
            if (sim != mark) {
                if (sist == 0) {
                    if (sidet != 0) { return 0 }
                }
            }
        }
        // Also check scale and offset
        var sc = (c.sc)
        var scm = (sc.mark)
        var scst = (sc.stay)
        var scdet = (sc.determinedBy)
        if (scm != mark) {
            if (scst == 0) {
                if (scdet != 0) { return 0 }
            }
        }
        var off = (c.off)
        var offm = (off.mark)
        var offst = (off.stay)
        var offdet = (off.determinedBy)
        if (offm != mark) {
            if (offst == 0) {
                if (offdet != 0) { return 0 }
            }
        }
    }
    return 1
}

pn c_recalculate(c) {
    var k = (c.kind)
    if (k == K_EDIT) {
        var o = (c.out)
        var cs = (c.strength)
        o.walkStrength = cs
        // isInput=true so stay=false → stay=0
        o.stay = 0
        return 0
    }
    if (k == K_STAY) {
        var o2 = (c.out)
        var cs2 = (c.strength)
        o2.walkStrength = cs2
        // isInput=false so stay=true → stay=1
        var ost = (o2.stay)
        o2.stay = 1
        // Stay execute is no-op, nothing to do
        return 0
    }
    if (k == K_EQUAL) {
        var dir = (c.direction)
        var ihn = null
        var out = null
        if (dir == FORWARD) {
            ihn = (c.v1)
            out = (c.v2)
        }
        if (dir == BACKWARD) {
            ihn = (c.v2)
            out = (c.v1)
        }
        var cs3 = (c.strength)
        var iws = (ihn.walkStrength)
        var ws = s_weakest(cs3, iws)
        out.walkStrength = ws
        var ist = (ihn.stay)
        out.stay = ist
        if (ist == 1) {
            c_execute(c)
        }
        return 0
    }
    if (k == K_SCALE) {
        var dir2 = (c.direction)
        var ihn2 = null
        var out2 = null
        if (dir2 == FORWARD) {
            ihn2 = (c.v1)
            out2 = (c.v2)
        }
        if (dir2 == BACKWARD) {
            ihn2 = (c.v2)
            out2 = (c.v1)
        }
        var cs4 = (c.strength)
        var iws2 = (ihn2.walkStrength)
        var ws2 = s_weakest(cs4, iws2)
        out2.walkStrength = ws2
        var ist2 = (ihn2.stay)
        var sc = (c.sc)
        var scst = (sc.stay)
        var off = (c.off)
        var offst = (off.stay)
        var stay = 1
        if (ist2 == 0) { stay = 0 }
        if (scst == 0) { stay = 0 }
        if (offst == 0) { stay = 0 }
        out2.stay = stay
        if (stay == 1) {
            c_execute(c)
        }
        return 0
    }
    return 0
}

pn c_execute(c) {
    var k = (c.kind)
    // Edit and Stay: no-op
    if (k == K_EDIT) { return 0 }
    if (k == K_STAY) { return 0 }
    if (k == K_EQUAL) {
        var dir = (c.direction)
        if (dir == FORWARD) {
            var v1 = (c.v1)
            var v2 = (c.v2)
            var val = (v1.val)
            v2.val = val
        }
        if (dir == BACKWARD) {
            var v1b = (c.v1)
            var v2b = (c.v2)
            var valb = (v2b.val)
            v1b.val = valb
        }
        return 0
    }
    if (k == K_SCALE) {
        var dirs = (c.direction)
        if (dirs == FORWARD) {
            var sv1 = (c.v1)
            var sv2 = (c.v2)
            var sc = (c.sc)
            var off = (c.off)
            var sv1v = (sv1.val)
            var scv = (sc.val)
            var offv = (off.val)
            var result = sv1v * scv + offv
            sv2.val = result
        }
        if (dirs == BACKWARD) {
            var sv1b = (c.v1)
            var sv2b = (c.v2)
            var scb = (c.sc)
            var offb = (c.off)
            var sv2v = (sv2b.val)
            var scvb = (scb.val)
            var offvb = (offb.val)
            var resultb = (sv2v - offvb) / scvb
            sv1b.val = resultb
        }
        return 0
    }
    return 0
}

// --- Planner ---
pn planner_new() {
    var p = { currentMark: 1, nextCid: 1 }
    return p
}

pn planner_new_mark(planner) {
    var cm = (planner.currentMark) + 1
    planner.currentMark = cm
    return cm
}

pn planner_next_cid(planner) {
    var nc = (planner.nextCid)
    var nn = nc + 1
    planner.nextCid = nn
    return nc
}

// c_satisfy: returns overridden constraint (or null)
pn c_satisfy(c, mark, planner) {
    c_choose_method(c, mark)
    var sat = c_is_satisfied(c)
    if (sat == 1) {
        c_mark_inputs(c, mark)
        var out = c_get_output(c)
        var det = (out.determinedBy)
        var overridden = null
        if (det != 0) {
            // find old constraint by cid
            var cs = (out.constraints)
            var csz = vec_size(cs)
            var i = 0
            while (i < csz) {
                var cc = vec_at(cs, i)
                var ccid = (cc.cid)
                if (ccid == det) {
                    overridden = cc
                }
                i = i + 1
            }
            if (overridden != null) {
                c_mark_unsatisfied(overridden)
            }
        }
        var ccid2 = (c.cid)
        out.determinedBy = ccid2
        var ok = planner_add_propagate(planner, c, mark)
        if (ok == 0) {
            print("ERROR: cycle\n")
        }
        out.mark = mark
        return overridden
    }
    var cs2 = (c.strength)
    if (cs2 == S_REQUIRED) {
        print("ERROR: required constraint not satisfied\n")
    }
    return null
}

pn c_add_constraint(c, planner) {
    c_add_to_graph(c)
    planner_incremental_add(planner, c)
}

pn c_destroy_constraint(c, planner) {
    var sat = c_is_satisfied(c)
    if (sat == 1) {
        planner_incremental_remove(planner, c)
    }
    c_remove_from_graph(c)
}

// --- Constraint constructors ---
pn edit_constraint_new(v, strength, planner) {
    var cid = planner_next_cid(planner)
    var c = { cid: 0, kind: 1, strength: 0, out: null, satisfied: 0 }
    c.cid = cid
    c.strength = strength
    c.out = v
    c_add_constraint(c, planner)
    return c
}

pn stay_constraint_new(v, strength, planner) {
    var cid = planner_next_cid(planner)
    var c = { cid: 0, kind: 2, strength: 0, out: null, satisfied: 0 }
    c.cid = cid
    c.strength = strength
    c.out = v
    c_add_constraint(c, planner)
    return c
}

pn equality_constraint_new(v1, v2, strength, planner) {
    var cid = planner_next_cid(planner)
    var c = { cid: 0, kind: 3, strength: 0, v1: null, v2: null, direction: 0 }
    c.cid = cid
    c.strength = strength
    c.v1 = v1
    c.v2 = v2
    c_add_constraint(c, planner)
    return c
}

pn scale_constraint_new(src, scale, offset, dest, strength, planner) {
    var cid = planner_next_cid(planner)
    var c = { cid: 0, kind: 4, strength: 0, v1: null, v2: null, direction: 0, sc: null, off: null }
    c.cid = cid
    c.strength = strength
    c.v1 = src
    c.v2 = dest
    c.sc = scale
    c.off = offset
    c_add_constraint(c, planner)
    return c
}

// --- Planner methods ---
pn planner_incremental_add(planner, c) {
    var mark = planner_new_mark(planner)
    var overridden = c_satisfy(c, mark, planner)
    while (overridden != null) {
        overridden = c_satisfy(overridden, mark, planner)
    }
}

pn planner_incremental_remove(planner, c) {
    var out = c_get_output(c)
    c_mark_unsatisfied(c)
    c_remove_from_graph(c)
    var unsatisfied = planner_remove_propagate_from(planner, out)
    var usz = vec_size(unsatisfied)
    var i = 0
    while (i < usz) {
        var u = vec_at(unsatisfied, i)
        planner_incremental_add(planner, u)
        i = i + 1
    }
}

pn planner_extract_plan(planner, constraints) {
    var sources = vec_new()
    var csz = vec_size(constraints)
    var i = 0
    while (i < csz) {
        var c = vec_at(constraints, i)
        var inp = c_is_input(c)
        var sat = c_is_satisfied(c)
        if (inp == 1) {
            if (sat == 1) {
                vec_add(sources, c)
            }
        }
        i = i + 1
    }
    var plan = planner_make_plan(planner, sources)
    return plan
}

pn planner_make_plan(planner, sources) {
    var mark = planner_new_mark(planner)
    var plan = vec_new()
    var todo = sources
    var empty = vec_is_empty(todo)
    while (empty == 0) {
        var c = vec_remove_first(todo)
        var out = c_get_output(c)
        var om = (out.mark)
        if (om != mark) {
            var ik = c_inputs_known(c, mark)
            if (ik == 1) {
                vec_add(plan, c)
                out.mark = mark
                planner_add_constraints_consuming_to(planner, out, todo)
            }
        }
        empty = vec_is_empty(todo)
    }
    return plan
}

pn planner_propagate_from(planner, v) {
    var todo = vec_new()
    planner_add_constraints_consuming_to(planner, v, todo)
    var empty = vec_is_empty(todo)
    while (empty == 0) {
        var c = vec_remove_first(todo)
        c_execute(c)
        var out = c_get_output(c)
        planner_add_constraints_consuming_to(planner, out, todo)
        empty = vec_is_empty(todo)
    }
}

pn planner_add_constraints_consuming_to(planner, v, coll) {
    var det = (v.determinedBy)
    var cs = (v.constraints)
    var csz = vec_size(cs)
    var i = 0
    while (i < csz) {
        var c = vec_at(cs, i)
        var ccid = (c.cid)
        if (ccid != det) {
            var sat = c_is_satisfied(c)
            if (sat == 1) {
                vec_add(coll, c)
            }
        }
        i = i + 1
    }
}

pn planner_add_propagate(planner, c, mark) {
    var todo = vec_with(c)
    var empty = vec_is_empty(todo)
    while (empty == 0) {
        var d = vec_remove_first(todo)
        var out = c_get_output(d)
        var om = (out.mark)
        if (om == mark) {
            planner_incremental_remove(planner, c)
            return 0
        }
        c_recalculate(d)
        planner_add_constraints_consuming_to(planner, out, todo)
        empty = vec_is_empty(todo)
    }
    return 1
}

pn planner_change(planner, v, newValue) {
    var editC = edit_constraint_new(v, S_PREFERRED, planner)
    var editV = vec_with(editC)
    var plan = planner_extract_plan(planner, editV)
    var i = 0
    while (i < 10) {
        v.val = newValue
        // execute plan
        var psz = vec_size(plan)
        var j = 0
        while (j < psz) {
            var pc = vec_at(plan, j)
            c_execute(pc)
            j = j + 1
        }
        i = i + 1
    }
    c_destroy_constraint(editC, planner)
}

pn planner_remove_propagate_from(planner, out) {
    var unsatisfied = vec_new()
    out.determinedBy = 0
    out.walkStrength = S_ABSOLUTE_WEAKEST
    out.stay = 1
    var todo = vec_with(out)
    var empty = vec_is_empty(todo)
    while (empty == 0) {
        var v = vec_remove_first(todo)
        var cs = (v.constraints)
        var csz = vec_size(cs)
        var i = 0
        while (i < csz) {
            var cc = vec_at(cs, i)
            var sat = c_is_satisfied(cc)
            if (sat == 0) {
                vec_add(unsatisfied, cc)
            }
            i = i + 1
        }
        // constraintsConsuming(v, fn): for each c != determinedBy && satisfied
        var det = (v.determinedBy)
        var j = 0
        while (j < csz) {
            var cc2 = vec_at(cs, j)
            var cc2id = (cc2.cid)
            if (cc2id != det) {
                var sat2 = c_is_satisfied(cc2)
                if (sat2 == 1) {
                    c_recalculate(cc2)
                    var o2 = c_get_output(cc2)
                    vec_add(todo, o2)
                }
            }
            j = j + 1
        }
        empty = vec_is_empty(todo)
    }
    // Skip sort — not needed for correctness
    return unsatisfied
}

// --- Benchmark tests ---

pn chain_test(n) {
    var planner = planner_new()
    // Create n+1 variables
    var vars = vec_new()
    var i = 0
    var np1 = n + 1
    while (i < np1) {
        var v = var_new()
        vec_add(vars, v)
        i = i + 1
    }
    // Build chain of n equality constraints
    var j = 0
    while (j < n) {
        var jp1 = j + 1
        var v1 = vec_at(vars, j)
        var v2 = vec_at(vars, jp1)
        var ec = equality_constraint_new(v1, v2, S_REQUIRED, planner)
        j = j + 1
    }
    // StayConstraint on last variable
    var last = vec_at(vars, n)
    var sc = stay_constraint_new(last, S_STRONG_DEFAULT, planner)
    // EditConstraint on first variable
    var first = vec_at(vars, 0)
    var editC = edit_constraint_new(first, S_PREFERRED, planner)
    var editV = vec_with(editC)
    var plan = planner_extract_plan(planner, editV)
    // Run 100 iterations
    var k = 0
    while (k < 100) {
        first.val = k
        // execute plan
        var psz = vec_size(plan)
        var m = 0
        while (m < psz) {
            var pc = vec_at(plan, m)
            c_execute(pc)
            m = m + 1
        }
        var lastval = (last.val)
        if (lastval != k) {
            print("Chain test FAILED at k=")
            print(k)
            print(" lastval=")
            print(lastval)
            print("\n")
            return 0
        }
        k = k + 1
    }
    c_destroy_constraint(editC, planner)
    return 1
}

pn projection_test(n) {
    var planner = planner_new()
    var dests = vec_new()
    var scale = var_value(10)
    var offset = var_value(1000)
    var src = null
    var dst = null
    var i = 1
    while (i <= n) {
        src = var_value(i)
        dst = var_value(i)
        vec_add(dests, dst)
        var stc = stay_constraint_new(src, S_DEFAULT, planner)
        var scc = scale_constraint_new(src, scale, offset, dst, S_REQUIRED, planner)
        i = i + 1
    }
    planner_change(planner, src, 17)
    var dstval = (dst.val)
    if (dstval != 1170) {
        print("Projection test 1 FAILED: dst=")
        print(dstval)
        print("\n")
        return 0
    }
    planner_change(planner, dst, 1050)
    var srcval = (src.val)
    if (srcval != 5) {
        print("Projection test 2 FAILED: src=")
        print(srcval)
        print("\n")
        return 0
    }
    planner_change(planner, scale, 5)
    var j = 0
    var nm1 = n - 1
    while (j < nm1) {
        var dj = vec_at(dests, j)
        var djv = (dj.val)
        var expected = (j + 1) * 5 + 1000
        if (djv != expected) {
            print("Projection test 3 FAILED at j=")
            print(j)
            print(" got=")
            print(djv)
            print(" expected=")
            print(expected)
            print("\n")
            return 0
        }
        j = j + 1
    }
    planner_change(planner, offset, 2000)
    var k = 0
    while (k < nm1) {
        var dk = vec_at(dests, k)
        var dkv = (dk.val)
        var expected2 = (k + 1) * 5 + 2000
        if (dkv != expected2) {
            print("Projection test 4 FAILED at k=")
            print(k)
            print(" got=")
            print(dkv)
            print(" expected=")
            print(expected2)
            print("\n")
            return 0
        }
        k = k + 1
    }
    return 1
}

pn main() {
    var r1 = chain_test(100)
    if (r1 == 0) {
        print("DeltaBlue: FAIL (chain)\n")
        return 0
    }
    var r2 = projection_test(100)
    if (r2 == 0) {
        print("DeltaBlue: FAIL (projection)\n")
        return 0
    }
    print("DeltaBlue: PASS\n")
}
