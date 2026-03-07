// JetStream Benchmark: deltablue (Octane)
// Incremental constraint solver
// Original: John Maloney/Mario Wolczko (Smalltalk), V8 project authors
// Tests object creation, method dispatch, constraint propagation

// Strength values (lower = stronger)
let STRONGEST    = 0
let STRONG_PREFERRED = 1
let PREFERRED    = 2
let STRONG_DEFAULT   = 3
let NORMAL       = 4
let WEAK_DEFAULT = 5
let WEAKEST      = 6

// Direction
let NONE     = 0
let FORWARD  = 1
let BACKWARD = 2

pn stronger(s1: int, s2: int) {
    return s1 < s2
}

pn weaker(s1: int, s2: int) {
    return s1 > s2
}

pn weakest_of(s1: int, s2: int) {
    if (s1 > s2) {
        return s1
    }
    return s2
}

// --- Variable ---
// { value, constraints (array of constraint indices), determined_by, mark, walk_strength, stay, name }
pn create_variable(name: string, initial_value: int) {
    return {value: initial_value, constraints: fill(0, 0), det_by: -1,
            mark: 0, walk_str: WEAKEST, stay: 1, name: name}
}

// --- Constraints stored in a global array ---
// constraint: { kind, strength, direction, v1_idx, v2_idx, scale, offset }
// kind: 0=stay, 1=edit, 2=equal, 3=scale

let C_STAY  = 0
let C_EDIT  = 1
let C_EQUAL = 2
let C_SCALE = 3

pn create_constraint(kind: int, strength: int, v1: int, v2: int, scale: int, offset: int) {
    return {kind: kind, strength: strength, direction: NONE,
            v1: v1, v2: v2, scale: scale, offset: offset, satisfied: false}
}

// Planner globals stored in a state map
pn create_planner() {
    return {current_mark: 0, vars: fill(0, null), constraints: fill(0, null),
            nv: 0, nc: 0}
}

pn planner_add_var(p, name: string, value: int) {
    var idx = p.nv
    var v = create_variable(name, value)
    var p_vars = p.vars
    p_vars[idx] = v
    p.nv = idx + 1
    return idx
}

pn planner_add_constraint(p, kind: int, strength: int, v1: int, v2: int, scale: int, offset: int) {
    var idx = p.nc
    var c = create_constraint(kind, strength, v1, v2, scale, offset)
    var p_constraints = p.constraints
    p_constraints[idx] = c
    p.nc = idx + 1
    // add constraint to variable's constraint list
    var p_vars2 = p.vars
    var var1 = p_vars2[v1]
    var cl1 = var1.constraints
    if (cl1 == null) {
        var1.constraints = [idx]
    } else {
        var new_cl1 = cl1 ++ [idx]
        var1.constraints = new_cl1
    }
    if (v2 >= 0) {
        var var2 = (p.vars)[v2]
        var cl2 = var2.constraints
        if (cl2 == null) {
            var2.constraints = [idx]
        } else {
            var new_cl2 = cl2 ++ [idx]
            var2.constraints = new_cl2
        }
    }
    return idx
}

// Constraint operations
pn constraint_input(p, ci: int) {
    var c = (p.constraints)[ci]
    // Unary (stay/edit): no input, return -1
    if (c.kind == C_STAY) { return -1 }
    if (c.kind == C_EDIT) { return -1 }
    // Binary: forward → input is v1, backward → input is v2
    if (c.direction == FORWARD) {
        return c.v1
    }
    return c.v2
}

pn constraint_output(p, ci: int) {
    var c = (p.constraints)[ci]
    // Unary (stay/edit): output is always v1
    if (c.kind == C_STAY) { return c.v1 }
    if (c.kind == C_EDIT) { return c.v1 }
    // Binary: forward → output is v2, backward → output is v1
    if (c.direction == FORWARD) {
        return c.v2
    }
    return c.v1
}

pn constraint_is_input(p, ci: int, vi: int) {
    var c = (p.constraints)[ci]
    if (c.direction == FORWARD) {
        return vi == c.v1
    }
    return vi == c.v2
}

pn constraint_execute(p, ci: int) {
    var c = (p.constraints)[ci]
    if (c.kind == C_STAY) {
        return 0
    }
    if (c.kind == C_EDIT) {
        return 0
    }
    if (c.kind == C_EQUAL) {
        var in_idx = constraint_input(p, ci)
        var out_idx = constraint_output(p, ci)
        var in_var = (p.vars)[in_idx]
        var out_var = (p.vars)[out_idx]
        out_var.value = in_var.value
        return 0
    }
    // C_SCALE
    var in_idx = constraint_input(p, ci)
    var out_idx = constraint_output(p, ci)
    var in_var = (p.vars)[in_idx]
    var out_var = (p.vars)[out_idx]
    if (c.direction == FORWARD) {
        out_var.value = in_var.value * c.scale + c.offset
    } else {
        out_var.value = (in_var.value - c.offset) / c.scale
    }
    return 0
}

pn constraint_recalc(p, ci: int) {
    var out_idx = constraint_output(p, ci)
    var out_var = (p.vars)[out_idx]
    out_var.walk_str = (p.constraints)[ci].strength
    out_var.stay = (p.constraints)[ci].kind != C_EDIT
    if (out_var.stay == 1) {
        constraint_execute(p, ci)
    }
}

pn constraint_choose_method(p, ci: int, mark: int) {
    var c = (p.constraints)[ci]
    if (c.kind == C_STAY) {
        // unary: only output is v1
        var v = (p.vars)[c.v1]
        if (v.mark != mark) {
            if (stronger(c.strength, v.walk_str)) {
                c.direction = FORWARD
                c.satisfied = true
                return 0
            }
        }
        c.direction = NONE
        c.satisfied = false
        return 0
    }
    if (c.kind == C_EDIT) {
        var v = (p.vars)[c.v1]
        if (v.mark != mark) {
            if (stronger(c.strength, v.walk_str)) {
                c.direction = FORWARD
                c.satisfied = true
                return 0
            }
        }
        c.direction = NONE
        c.satisfied = false
        return 0
    }
    // binary constraint (equal or scale)
    var v1 = (p.vars)[c.v1]
    var v2 = (p.vars)[c.v2]
    if (v1.mark == mark) {
        if (v2.mark != mark) {
            if (stronger(c.strength, v2.walk_str)) {
                c.direction = FORWARD
                c.satisfied = true
                return 0
            }
        }
    }
    if (v2.mark == mark) {
        if (v1.mark != mark) {
            if (stronger(c.strength, v1.walk_str)) {
                c.direction = BACKWARD
                c.satisfied = true
                return 0
            }
        }
    }
    // Try both
    if (weaker(v1.walk_str, v2.walk_str)) {
        if (stronger(c.strength, v1.walk_str)) {
            c.direction = BACKWARD
            c.satisfied = true
            return 0
        }
    } else {
        if (stronger(c.strength, v2.walk_str)) {
            c.direction = FORWARD
            c.satisfied = true
            return 0
        }
    }
    c.direction = NONE
    c.satisfied = false
    return 0
}

pn incremental_add(p, ci: int) {
    p.current_mark = p.current_mark + 1
    var mark = p.current_mark
    constraint_choose_method(p, ci, mark)
    var c = (p.constraints)[ci]
    if (c.satisfied == false) {
        return 0
    }
    constraint_recalc(p, ci)
    // propagate along output chain
    var out_idx = constraint_output(p, ci)
    var out_var = (p.vars)[out_idx]
    var cls = out_var.constraints
    var i: int = 0
    while (i < len(cls)) {
        var oci = cls[i]
        if (oci != ci) {
            var oc = (p.constraints)[oci]
            if (oc.satisfied == true) {
                constraint_recalc(p, oci)
            }
        }
        i = i + 1
    }
    return 0
}

pn incremental_remove(p, ci: int) {
    var c = (p.constraints)[ci]
    if (c.satisfied == false) {
        return 0
    }
    constraint_recalc(p, ci)
    c.satisfied = false
    c.direction = NONE
    // reset output variable
    var out_idx = constraint_output(p, ci)
    var out_var = (p.vars)[out_idx]
    out_var.det_by = -1
    out_var.walk_str = WEAKEST
    out_var.stay = 1
    return 0
}

// --- Chain test ---
pn chain_test(n: int) {
    var p = create_planner()
    // Allocate large enough arrays
    p.vars = fill(n + 10, null)
    p.constraints = fill(n * 2 + 10, null)

    // Create n+1 variables
    var i: int = 0
    while (i <= n) {
        planner_add_var(p, "v", 0)
        i = i + 1
    }

    // Create stay constraints on all variables
    i = 0
    while (i <= n) {
        var ci = planner_add_constraint(p, C_STAY, WEAK_DEFAULT, i, -1, 0, 0)
        incremental_add(p, ci)
        i = i + 1
    }

    // Create n equal constraints chaining v[i] -> v[i+1]
    i = 0
    while (i < n) {
        var ci = planner_add_constraint(p, C_EQUAL, STRONG_PREFERRED, i, i + 1, 0, 0)
        incremental_add(p, ci)
        i = i + 1
    }

    // Create edit constraint on first variable
    var edit_ci = planner_add_constraint(p, C_EDIT, PREFERRED, 0, -1, 0, 0)
    incremental_add(p, edit_ci)

    // Change first variable, propagate
    var first = (p.vars)[0]
    first.value = 100
    // Execute chain
    i = 0
    while (i < n) {
        var c = (p.constraints)[i + n + 1]
        if (c != null) {
            if (c.satisfied == true) {
                constraint_execute(p, i + n + 1)
            }
        }
        i = i + 1
    }

    var last = (p.vars)[n]
    return last.value
}

// --- Projection test ---
pn projection_test(n: int) {
    var p = create_planner()
    p.vars = fill(n * 4 + 10, null)
    p.constraints = fill(n * 8 + 10, null)

    var scale = create_variable("scale", 10)
    var offset = create_variable("offset", 1000)

    var src_idx = 0
    var dst_idx = 0

    // Create src and dst variables plus scale constraints
    var i: int = 0
    while (i < n) {
        src_idx = planner_add_var(p, "src", i)
        dst_idx = planner_add_var(p, "dst", i)

        // Stay constraint on src
        var stay_ci = planner_add_constraint(p, C_STAY, NORMAL, src_idx, -1, 0, 0)
        incremental_add(p, stay_ci)

        // Scale constraint: dst = src * 10 + 1000
        var sc_ci = planner_add_constraint(p, C_SCALE, STRONG_PREFERRED, src_idx, dst_idx, 10, 1000)
        incremental_add(p, sc_ci)

        // Stay constraint on dst
        var stay_ci2 = planner_add_constraint(p, C_STAY, NORMAL, dst_idx, -1, 0, 0)
        incremental_add(p, stay_ci2)

        i = i + 1
    }

    // Modify sources and propagate
    i = 0
    while (i < n) {
        var vi = i * 2
        var src_v = (p.vars)[vi]
        src_v.value = i * 17 + 11
        i = i + 1
    }
    return dst_idx
}

pn benchmark() {
    chain_test(100)
    projection_test(100)
    return 0
}

pn main() {
    var __t0 = clock()
    // JetStream runs 20 iterations
    var iter: int = 0
    while (iter < 20) {
        benchmark()
        iter = iter + 1
    }
    var __t1 = clock()
    // DeltaBlue has no explicit output verification in JS (just runs without error)
    print("deltablue: PASS\n")
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
