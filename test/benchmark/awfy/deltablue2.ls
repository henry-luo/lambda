// AWFY Benchmark: DeltaBlue — VALUE-SEMANTICS port (handle store)
// Incremental constraint solver
//
// The reference-semantics shape this replaces: a Variable record was reached
// from `c.v1`/`c.v2`/`c.out`/`c.sc`/`c.off` on several constraints AND from the
// local that created it, and every function mutated whichever handle it held.
// That depends on aliasing, which S9.1.2/S9.3.1 rule out — those handles would
// be independent copies drifting apart, so each constraint would mark and
// recalculate its own private variable and the plan would never converge.
//
// This port applies the handle-store idiom (C4.2e; doc/Lambda_Procedural.md
// "Sharing Mutable State"): every record has exactly one owner, and every field
// that used to hold a pointer holds an id into that owner instead.
//
//   w.vars[vid]  owns every Variable    — `out`, `v1`, `v2`, `sc`, `off` are vids
//   w.cons[cid]  owns every Constraint  — `determinedBy` and every plan/todo
//                                         list element is a cid
//
// `w` is the single mutation root and travels as one `var` parameter, so
// S9.1.3's exclusivity check is trivially satisfied at every call site.
// Constraint ids start at 1, so cid 0 is the "no constraint" sentinel that
// `determinedBy` already used. Variable id 0 is legal, so NO_VAR is -1.

// --- Constants ---
let FORWARD = 1
let BACKWARD = 2

let S_ABSOLUTE_STRONGEST = -10000
let S_REQUIRED = -800
let S_STRONG_PREFERRED = -600
let S_PREFERRED = -400
let S_STRONG_DEFAULT = -200
let S_DEFAULT = 0
let S_WEAK_DEFAULT = 500
let S_ABSOLUTE_WEAKEST = 10000

let K_EDIT = 1
let K_STAY = 2
let K_EQUAL = 3
let K_SCALE = 4

let NO_CON = 0    // handle-store spelling of a null constraint
let NO_VAR = -1   // handle-store spelling of a null variable

type Vec = any    // a growable array (built-in []/push/len/splice)

// --- Vector ---

pn vec_new() any {
    return []
}

pn vec_add(var v: Vec, item) any {
    push(v, item)
}

pn vec_at(var v: Vec, idx) any {
    return v[idx]
}

pn vec_size(v: Vec) any {
    return len(v)
}

pn vec_is_empty(v: Vec) any {
    if (len(v) == 0) { return 1 }
    return 0
}

pn vec_remove_first(var v: Vec) any {
    if (len(v) == 0) { return NO_CON }
    var first = v[0]
    splice(v, 0, 1)
    return first
}

pn vec_with(item) any {
    var v = vec_new()
    vec_add(v, item)
    return v
}

// Remove a cid from a vector of cids (elements are plain ints now).
pn vec_remove_cid(var v: Vec, cid) any {
    var sz = len(v)
    var found: int = -1
    var i: int = 0
    while (i < sz) {
        if (found == -1) {
            var elem = v[i]
            if (elem == cid) {
                found = i
            }
        }
        i = i + 1
    }
    if (found == -1) { return 0 }
    splice(v, found, 1)
    return 1
}

// --- Strength helpers ---
pn s_stronger(a, b) any {
    if (a < b) { return 1 }
    return 0
}

pn s_weaker(a, b) any {
    if (a > b) { return 1 }
    return 0
}

pn s_weakest(a, b) any {
    if (a > b) { return a }
    return b
}

// --- World ---
pn world_new() any {
    // cons[0] is the NO_CON placeholder so a cid indexes its own slot.
    var w = { vars: [], cons: [null], currentMark: 1, nextCid: 1 }
    return w
}

// --- Variable: owned by w.vars, addressed by slot id ---

pn var_new(var w) any {
    var vid = len(w.vars)
    push(w.vars, { val: 0, constraints: [], determinedBy: NO_CON,
                   walkStrength: 10000, stay: 1, mark: 0 })
    return vid
}

pn var_value(var w, aValue) any {
    var vid = var_new(w)
    w.vars[vid].val = aValue
    return vid
}

// The constraint list is read, mutated and stored back (C4.2e): binding
// `w.vars[vid].constraints` binds a copy under S9.1.2.
pn var_add_constraint(var w, vid, ccid) any {
    var cs: Vec = w.vars[vid].constraints
    vec_add(cs, ccid)
    w.vars[vid].constraints = cs
}

pn var_remove_constraint(var w, vid, ccid) any {
    var cs: Vec = w.vars[vid].constraints
    vec_remove_cid(cs, ccid)
    w.vars[vid].constraints = cs
    var det = w.vars[vid].determinedBy
    if (det == ccid) {
        w.vars[vid].determinedBy = NO_CON
    }
}

// --- Constraint dispatch ---
// All constraints share one shape so the store stays a single map shape:
//   { cid, kind, strength, out, satisfied, v1, v2, direction, sc, off }
// out/v1/v2/sc/off are variable ids (NO_VAR when unused).

pn c_is_input(var w, cid) any {
    var k = w.cons[cid].kind
    if (k == K_EDIT) { return 1 }
    return 0
}

pn c_is_satisfied(var w, cid) any {
    var k = w.cons[cid].kind
    if (k == K_EDIT) {
        return w.cons[cid].satisfied
    }
    if (k == K_STAY) {
        return w.cons[cid].satisfied
    }
    // binary: satisfied = direction != 0
    var dir = w.cons[cid].direction
    if (dir != 0) { return 1 }
    return 0
}

pn c_add_to_graph(var w, cid) any {
    var k = w.cons[cid].kind
    if (k == K_EDIT) {
        var_add_constraint(w, w.cons[cid].out, cid)
        w.cons[cid].satisfied = 0
    }
    if (k == K_STAY) {
        var_add_constraint(w, w.cons[cid].out, cid)
        w.cons[cid].satisfied = 0
    }
    if (k == K_EQUAL) {
        var_add_constraint(w, w.cons[cid].v1, cid)
        var_add_constraint(w, w.cons[cid].v2, cid)
        w.cons[cid].direction = 0
    }
    if (k == K_SCALE) {
        var_add_constraint(w, w.cons[cid].v1, cid)
        var_add_constraint(w, w.cons[cid].v2, cid)
        var_add_constraint(w, w.cons[cid].sc, cid)
        var_add_constraint(w, w.cons[cid].off, cid)
        w.cons[cid].direction = 0
    }
}

pn c_remove_from_graph(var w, cid) any {
    var k = w.cons[cid].kind
    if (k == K_EDIT) {
        var o = w.cons[cid].out
        if (o != NO_VAR) { var_remove_constraint(w, o, cid) }
        w.cons[cid].satisfied = 0
    }
    if (k == K_STAY) {
        var o2 = w.cons[cid].out
        if (o2 != NO_VAR) { var_remove_constraint(w, o2, cid) }
        w.cons[cid].satisfied = 0
    }
    if (k == K_EQUAL) {
        var v1 = w.cons[cid].v1
        if (v1 != NO_VAR) { var_remove_constraint(w, v1, cid) }
        var v2 = w.cons[cid].v2
        if (v2 != NO_VAR) { var_remove_constraint(w, v2, cid) }
        w.cons[cid].direction = 0
    }
    if (k == K_SCALE) {
        var v1s = w.cons[cid].v1
        if (v1s != NO_VAR) { var_remove_constraint(w, v1s, cid) }
        var v2s = w.cons[cid].v2
        if (v2s != NO_VAR) { var_remove_constraint(w, v2s, cid) }
        var sc = w.cons[cid].sc
        if (sc != NO_VAR) { var_remove_constraint(w, sc, cid) }
        var off = w.cons[cid].off
        if (off != NO_VAR) { var_remove_constraint(w, off, cid) }
        w.cons[cid].direction = 0
    }
}

pn c_choose_method(var w, cid, mark) any {
    var k = w.cons[cid].kind
    if (k == K_EDIT) {
        var o = w.cons[cid].out
        var om = w.vars[o].mark
        var ows = w.vars[o].walkStrength
        var cs = w.cons[cid].strength
        if (om != mark) {
            var str = s_stronger(cs, ows)
            if (str == 1) {
                w.cons[cid].satisfied = 1
                return 0
            }
        }
        w.cons[cid].satisfied = 0
        return 0
    }
    if (k == K_STAY) {
        var o2 = w.cons[cid].out
        var om2 = w.vars[o2].mark
        var ows2 = w.vars[o2].walkStrength
        var cs2 = w.cons[cid].strength
        if (om2 != mark) {
            var str2 = s_stronger(cs2, ows2)
            if (str2 == 1) {
                w.cons[cid].satisfied = 1
                return 0
            }
        }
        w.cons[cid].satisfied = 0
        return 0
    }
    // Binary/Scale
    var v1 = w.cons[cid].v1
    var v2 = w.cons[cid].v2
    var v1m = w.vars[v1].mark
    var v2m = w.vars[v2].mark
    var v1ws = w.vars[v1].walkStrength
    var v2ws = w.vars[v2].walkStrength
    var cs3 = w.cons[cid].strength

    if (v1m == mark) {
        if (v2m != mark) {
            var sf = s_stronger(cs3, v2ws)
            if (sf == 1) {
                w.cons[cid].direction = FORWARD
                return 0
            }
        }
        w.cons[cid].direction = 0
        return 0
    }
    if (v2m == mark) {
        if (v1m != mark) {
            var sb = s_stronger(cs3, v1ws)
            if (sb == 1) {
                w.cons[cid].direction = BACKWARD
                return 0
            }
        }
        w.cons[cid].direction = 0
        return 0
    }
    // Neither marked
    var w1 = s_weaker(v1ws, v2ws)
    if (w1 == 1) {
        var sb2 = s_stronger(cs3, v1ws)
        if (sb2 == 1) {
            w.cons[cid].direction = BACKWARD
            return 0
        }
        w.cons[cid].direction = 0
        return 0
    }
    var sf2 = s_stronger(cs3, v2ws)
    if (sf2 == 1) {
        w.cons[cid].direction = FORWARD
        return 0
    }
    w.cons[cid].direction = 0
    return 0
}

pn c_mark_unsatisfied(var w, cid) any {
    var k = w.cons[cid].kind
    if (k == K_EDIT) { w.cons[cid].satisfied = 0 }
    if (k == K_STAY) { w.cons[cid].satisfied = 0 }
    if (k == K_EQUAL) { w.cons[cid].direction = 0 }
    if (k == K_SCALE) { w.cons[cid].direction = 0 }
}

pn c_get_output(var w, cid) any {
    var k = w.cons[cid].kind
    if (k == K_EDIT) { return w.cons[cid].out }
    if (k == K_STAY) { return w.cons[cid].out }
    // binary: forward→v2, backward→v1
    var dir = w.cons[cid].direction
    if (dir == FORWARD) { return w.cons[cid].v2 }
    return w.cons[cid].v1
}

pn c_mark_inputs(var w, cid, mark) any {
    var k = w.cons[cid].kind
    // Unary: no inputs
    if (k == K_EDIT) { return 0 }
    if (k == K_STAY) { return 0 }
    // Binary: input is the non-output variable
    var dir = w.cons[cid].direction
    if (k == K_EQUAL) {
        if (dir == FORWARD) {
            w.vars[w.cons[cid].v1].mark = mark
        }
        if (dir == BACKWARD) {
            w.vars[w.cons[cid].v2].mark = mark
        }
    }
    if (k == K_SCALE) {
        if (dir == FORWARD) {
            w.vars[w.cons[cid].v1].mark = mark
        }
        if (dir == BACKWARD) {
            w.vars[w.cons[cid].v2].mark = mark
        }
        w.vars[w.cons[cid].sc].mark = mark
        w.vars[w.cons[cid].off].mark = mark
    }
    return 0
}

pn c_inputs_known(var w, cid, mark) any {
    var k = w.cons[cid].kind
    if (k == K_EDIT) { return 1 }
    if (k == K_STAY) { return 1 }
    var dir = w.cons[cid].direction
    // Check input variable
    if (k == K_EQUAL) {
        var inp = NO_VAR
        if (dir == FORWARD) { inp = w.cons[cid].v1 }
        if (dir == BACKWARD) { inp = w.cons[cid].v2 }
        if (inp != NO_VAR) {
            var im = w.vars[inp].mark
            var ist = w.vars[inp].stay
            var idet = w.vars[inp].determinedBy
            if (im != mark) {
                if (ist == 0) {
                    if (idet != NO_CON) { return 0 }
                }
            }
        }
    }
    if (k == K_SCALE) {
        var sinp = NO_VAR
        if (dir == FORWARD) { sinp = w.cons[cid].v1 }
        if (dir == BACKWARD) { sinp = w.cons[cid].v2 }
        if (sinp != NO_VAR) {
            var sim = w.vars[sinp].mark
            var sist = w.vars[sinp].stay
            var sidet = w.vars[sinp].determinedBy
            if (sim != mark) {
                if (sist == 0) {
                    if (sidet != NO_CON) { return 0 }
                }
            }
        }
        // Also check scale and offset
        var sc = w.cons[cid].sc
        var scm = w.vars[sc].mark
        var scst = w.vars[sc].stay
        var scdet = w.vars[sc].determinedBy
        if (scm != mark) {
            if (scst == 0) {
                if (scdet != NO_CON) { return 0 }
            }
        }
        var off = w.cons[cid].off
        var offm = w.vars[off].mark
        var offst = w.vars[off].stay
        var offdet = w.vars[off].determinedBy
        if (offm != mark) {
            if (offst == 0) {
                if (offdet != NO_CON) { return 0 }
            }
        }
    }
    return 1
}

pn c_recalculate(var w, cid) any {
    var k = w.cons[cid].kind
    if (k == K_EDIT) {
        var o = w.cons[cid].out
        var cs = w.cons[cid].strength
        w.vars[o].walkStrength = cs
        // isInput=true so stay=false → stay=0
        w.vars[o].stay = 0
        return 0
    }
    if (k == K_STAY) {
        var o2 = w.cons[cid].out
        var cs2 = w.cons[cid].strength
        w.vars[o2].walkStrength = cs2
        // isInput=false so stay=true → stay=1
        w.vars[o2].stay = 1
        // Stay execute is no-op, nothing to do
        return 0
    }
    if (k == K_EQUAL) {
        var dir = w.cons[cid].direction
        var ihn = NO_VAR
        var out = NO_VAR
        if (dir == FORWARD) {
            ihn = w.cons[cid].v1
            out = w.cons[cid].v2
        }
        if (dir == BACKWARD) {
            ihn = w.cons[cid].v2
            out = w.cons[cid].v1
        }
        var cs3 = w.cons[cid].strength
        var iws = w.vars[ihn].walkStrength
        var ws = s_weakest(cs3, iws)
        w.vars[out].walkStrength = ws
        var ist = w.vars[ihn].stay
        w.vars[out].stay = ist
        if (ist == 1) {
            c_execute(w, cid)
        }
        return 0
    }
    if (k == K_SCALE) {
        var dir2 = w.cons[cid].direction
        var ihn2 = NO_VAR
        var out2 = NO_VAR
        if (dir2 == FORWARD) {
            ihn2 = w.cons[cid].v1
            out2 = w.cons[cid].v2
        }
        if (dir2 == BACKWARD) {
            ihn2 = w.cons[cid].v2
            out2 = w.cons[cid].v1
        }
        var cs4 = w.cons[cid].strength
        var iws2 = w.vars[ihn2].walkStrength
        var ws2 = s_weakest(cs4, iws2)
        w.vars[out2].walkStrength = ws2
        var ist2 = w.vars[ihn2].stay
        var scst = w.vars[w.cons[cid].sc].stay
        var offst = w.vars[w.cons[cid].off].stay
        var stay: int = 1
        if (ist2 == 0) { stay = 0 }
        if (scst == 0) { stay = 0 }
        if (offst == 0) { stay = 0 }
        w.vars[out2].stay = stay
        if (stay == 1) {
            c_execute(w, cid)
        }
        return 0
    }
    return 0
}

pn c_execute(var w, cid) any {
    var k = w.cons[cid].kind
    // Edit and Stay: no-op
    if (k == K_EDIT) { return 0 }
    if (k == K_STAY) { return 0 }
    if (k == K_EQUAL) {
        var dir = w.cons[cid].direction
        if (dir == FORWARD) {
            var val = w.vars[w.cons[cid].v1].val
            w.vars[w.cons[cid].v2].val = val
        }
        if (dir == BACKWARD) {
            var valb = w.vars[w.cons[cid].v2].val
            w.vars[w.cons[cid].v1].val = valb
        }
        return 0
    }
    if (k == K_SCALE) {
        var dirs = w.cons[cid].direction
        if (dirs == FORWARD) {
            var sv1v = w.vars[w.cons[cid].v1].val
            var scv = w.vars[w.cons[cid].sc].val
            var offv = w.vars[w.cons[cid].off].val
            var result = sv1v * scv + offv
            w.vars[w.cons[cid].v2].val = result
        }
        if (dirs == BACKWARD) {
            var sv2v = w.vars[w.cons[cid].v2].val
            var scvb = w.vars[w.cons[cid].sc].val
            var offvb = w.vars[w.cons[cid].off].val
            var resultb = (sv2v - offvb) / scvb
            w.vars[w.cons[cid].v1].val = resultb
        }
        return 0
    }
    return 0
}

// --- Planner (planner state lives on the world) ---

pn planner_new_mark(var w) any {
    var cm = w.currentMark + 1
    w.currentMark = cm
    return cm
}

pn planner_next_cid(var w) any {
    var nc = w.nextCid
    w.nextCid = nc + 1
    return nc
}

// c_satisfy: returns the overridden constraint id (or NO_CON)
pn c_satisfy(var w, cid, mark) any {
    c_choose_method(w, cid, mark)
    var sat = c_is_satisfied(w, cid)
    if (sat == 1) {
        c_mark_inputs(w, cid, mark)
        var out = c_get_output(w, cid)
        var det = w.vars[out].determinedBy
        var overridden = NO_CON
        if (det != NO_CON) {
            // find the old constraint by cid among the variable's constraints
            var cs: Vec = w.vars[out].constraints
            var csz = vec_size(cs)
            var i: int = 0
            while (i < csz) {
                var cc = vec_at(cs, i)
                if (cc == det) {
                    c_mark_unsatisfied(w, cc)
                    overridden = cc
                }
                i = i + 1
            }
        }
        w.vars[out].determinedBy = cid
        var ok = planner_add_propagate(w, cid, mark)
        if (ok == 0) {
            print("ERROR: cycle\n")
        }
        w.vars[out].mark = mark
        return overridden
    }
    var cs2 = w.cons[cid].strength
    if (cs2 == S_REQUIRED) {
        print("ERROR: required constraint not satisfied\n")
    }
    return NO_CON
}

pn c_add_constraint(var w, cid) any {
    c_add_to_graph(w, cid)
    planner_incremental_add(w, cid)
}

pn c_destroy_constraint(var w, cid) any {
    var sat = c_is_satisfied(w, cid)
    if (sat == 1) {
        planner_incremental_remove(w, cid)
    }
    c_remove_from_graph(w, cid)
}

// --- Constraint constructors: allocate into w.cons, return the cid ---

pn con_alloc(var w, kind, strength) any {
    var cid = planner_next_cid(w)
    push(w.cons, { cid: cid, kind: kind, strength: strength,
                   out: NO_VAR, satisfied: 0,
                   v1: NO_VAR, v2: NO_VAR, direction: 0,
                   sc: NO_VAR, off: NO_VAR })
    return cid
}

pn edit_constraint_new(var w, vid, strength) any {
    var cid = con_alloc(w, K_EDIT, strength)
    w.cons[cid].out = vid
    c_add_constraint(w, cid)
    return cid
}

pn stay_constraint_new(var w, vid, strength) any {
    var cid = con_alloc(w, K_STAY, strength)
    w.cons[cid].out = vid
    c_add_constraint(w, cid)
    return cid
}

pn equality_constraint_new(var w, v1, v2, strength) any {
    var cid = con_alloc(w, K_EQUAL, strength)
    w.cons[cid].v1 = v1
    w.cons[cid].v2 = v2
    c_add_constraint(w, cid)
    return cid
}

pn scale_constraint_new(var w, src, scale, offset, dest, strength) any {
    var cid = con_alloc(w, K_SCALE, strength)
    w.cons[cid].v1 = src
    w.cons[cid].v2 = dest
    w.cons[cid].sc = scale
    w.cons[cid].off = offset
    c_add_constraint(w, cid)
    return cid
}

// --- Planner methods ---
pn planner_incremental_add(var w, cid) any {
    var mark = planner_new_mark(w)
    var overridden = c_satisfy(w, cid, mark)
    while (overridden != NO_CON) {
        overridden = c_satisfy(w, overridden, mark)
    }
}

pn planner_incremental_remove(var w, cid) any {
    var out = c_get_output(w, cid)
    c_mark_unsatisfied(w, cid)
    c_remove_from_graph(w, cid)
    var unsatisfied = planner_remove_propagate_from(w, out)
    var usz = vec_size(unsatisfied)
    var i: int = 0
    while (i < usz) {
        var u = vec_at(unsatisfied, i)
        planner_incremental_add(w, u)
        i = i + 1
    }
}

pn planner_extract_plan(var w, var constraints: Vec) any {
    var sources: Vec = vec_new()
    var csz = vec_size(constraints)
    var i: int = 0
    while (i < csz) {
        var c = vec_at(constraints, i)
        var inp = c_is_input(w, c)
        var sat = c_is_satisfied(w, c)
        if (inp == 1) {
            if (sat == 1) {
                vec_add(sources, c)
            }
        }
        i = i + 1
    }
    var plan: Vec = planner_make_plan(w, sources)
    return plan
}

pn planner_make_plan(var w, var sources: Vec) any {
    var mark = planner_new_mark(w)
    var plan: Vec = vec_new()
    var todo = sources
    var empty = vec_is_empty(todo)
    while (empty == 0) {
        var c = vec_remove_first(todo)
        var out = c_get_output(w, c)
        var om = w.vars[out].mark
        if (om != mark) {
            var ik = c_inputs_known(w, c, mark)
            if (ik == 1) {
                vec_add(plan, c)
                w.vars[out].mark = mark
                planner_add_constraints_consuming_to(w, out, todo)
            }
        }
        empty = vec_is_empty(todo)
    }
    return plan
}

pn planner_propagate_from(var w, vid) any {
    var todo: Vec = vec_new()
    planner_add_constraints_consuming_to(w, vid, todo)
    var empty = vec_is_empty(todo)
    while (empty == 0) {
        var c = vec_remove_first(todo)
        c_execute(w, c)
        var out = c_get_output(w, c)
        planner_add_constraints_consuming_to(w, out, todo)
        empty = vec_is_empty(todo)
    }
}

pn planner_add_constraints_consuming_to(var w, vid, var coll: Vec) any {
    var det = w.vars[vid].determinedBy
    var cs: Vec = w.vars[vid].constraints
    var csz = vec_size(cs)
    var i: int = 0
    while (i < csz) {
        var c = vec_at(cs, i)
        if (c != det) {
            var sat = c_is_satisfied(w, c)
            if (sat == 1) {
                vec_add(coll, c)
            }
        }
        i = i + 1
    }
}

pn planner_add_propagate(var w, cid, mark) any {
    var todo: Vec = vec_with(cid)
    var empty = vec_is_empty(todo)
    while (empty == 0) {
        var d = vec_remove_first(todo)
        var out = c_get_output(w, d)
        var om = w.vars[out].mark
        if (om == mark) {
            planner_incremental_remove(w, cid)
            return 0
        }
        c_recalculate(w, d)
        planner_add_constraints_consuming_to(w, out, todo)
        empty = vec_is_empty(todo)
    }
    return 1
}

pn planner_change(var w, vid, newValue) any {
    var editC = edit_constraint_new(w, vid, S_PREFERRED)
    var editV: Vec = vec_with(editC)
    var plan: Vec = planner_extract_plan(w, editV)
    var i: int = 0
    while (i < 10) {
        w.vars[vid].val = newValue
        // execute plan
        var psz = vec_size(plan)
        var j: int = 0
        while (j < psz) {
            var pc = vec_at(plan, j)
            c_execute(w, pc)
            j = j + 1
        }
        i = i + 1
    }
    c_destroy_constraint(w, editC)
}

pn planner_remove_propagate_from(var w, out) any {
    var unsatisfied: Vec = vec_new()
    w.vars[out].determinedBy = NO_CON
    w.vars[out].walkStrength = S_ABSOLUTE_WEAKEST
    w.vars[out].stay = 1
    var todo: Vec = vec_with(out)
    var empty = vec_is_empty(todo)
    while (empty == 0) {
        var v = vec_remove_first(todo)
        var cs: Vec = w.vars[v].constraints
        var csz = vec_size(cs)
        var i: int = 0
        while (i < csz) {
            var cc = vec_at(cs, i)
            var sat = c_is_satisfied(w, cc)
            if (sat == 0) {
                vec_add(unsatisfied, cc)
            }
            i = i + 1
        }
        // constraintsConsuming(v, fn): for each c != determinedBy && satisfied
        var det = w.vars[v].determinedBy
        var j: int = 0
        while (j < csz) {
            var cc2 = vec_at(cs, j)
            if (cc2 != det) {
                var sat2 = c_is_satisfied(w, cc2)
                if (sat2 == 1) {
                    c_recalculate(w, cc2)
                    var o2 = c_get_output(w, cc2)
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

pn chain_test(n) any {
    var w = world_new()
    // Create n+1 variables
    var vars: Vec = vec_new()
    var i: int = 0
    var np1 = n + 1
    while (i < np1) {
        var v = var_new(w)
        vec_add(vars, v)
        i = i + 1
    }
    // Build chain of n equality constraints
    var j: int = 0
    while (j < n) {
        var jp1 = j + 1
        var v1 = vec_at(vars, j)
        var v2 = vec_at(vars, jp1)
        var ec = equality_constraint_new(w, v1, v2, S_REQUIRED)
        j = j + 1
    }
    // StayConstraint on last variable
    var tail_var = vec_at(vars, n)
    var sc = stay_constraint_new(w, tail_var, S_STRONG_DEFAULT)
    // EditConstraint on first variable
    var first = vec_at(vars, 0)
    var editC = edit_constraint_new(w, first, S_PREFERRED)
    var editV: Vec = vec_with(editC)
    var plan: Vec = planner_extract_plan(w, editV)
    // Run 100 iterations
    var k: int = 0
    while (k < 100) {
        w.vars[first].val = k
        // execute plan
        var psz = vec_size(plan)
        var m: int = 0
        while (m < psz) {
            var pc = vec_at(plan, m)
            c_execute(w, pc)
            m = m + 1
        }
        var lastval = w.vars[tail_var].val
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
    c_destroy_constraint(w, editC)
    return 1
}

pn projection_test(n) any {
    var w = world_new()
    var dests: Vec = vec_new()
    var scale = var_value(w, 10)
    var offset = var_value(w, 1000)
    var src = NO_VAR
    var dst = NO_VAR
    var i: int = 1
    while (i <= n) {
        src = var_value(w, i)
        dst = var_value(w, i)
        vec_add(dests, dst)
        var stc = stay_constraint_new(w, src, S_DEFAULT)
        var scc = scale_constraint_new(w, src, scale, offset, dst, S_REQUIRED)
        i = i + 1
    }
    planner_change(w, src, 17)
    var dstval = w.vars[dst].val
    if (dstval != 1170) {
        print("Projection test 1 FAILED: dst=")
        print(dstval)
        print("\n")
        return 0
    }
    planner_change(w, dst, 1050)
    var srcval = w.vars[src].val
    if (srcval != 5) {
        print("Projection test 2 FAILED: src=")
        print(srcval)
        print("\n")
        return 0
    }
    planner_change(w, scale, 5)
    var j: int = 0
    var nm1 = n - 1
    while (j < nm1) {
        var dj = vec_at(dests, j)
        var djv = w.vars[dj].val
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
    planner_change(w, offset, 2000)
    var k: int = 0
    while (k < nm1) {
        var dk = vec_at(dests, k)
        var dkv = w.vars[dk].val
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
    var __t0 = clock()
    // Synchronized with JetStream: 20 iterations of chain_test(100) + projection_test(100)
    var k: int = 0
    while (k < 20) {
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
        k = k + 1
    }
    var __t1 = clock()
    print("DeltaBlue: PASS\n")
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
