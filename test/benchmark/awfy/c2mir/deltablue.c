/* Native C2MIR port of awfy/deltablue.ls. */
/* One-way constraint solver. Faithful port of the constraint class hierarchy:
 * a single tagged Constraint record with switch dispatch on `kind` (edit /
 * stay / equality / scale), matching the .ls kind-dispatched object model.
 * Workload matches the .ls exactly: 20 outer iterations of chain_test(100)
 * and projection_test(100). Objects and growable vectors come from a bump
 * arena that is reset at the start of each test invocation. */
extern int printf(const char *, ...);

#define FORWARD 1
#define BACKWARD 2
/* strength: arithmetic values, lower = stronger */
#define S_REQUIRED (-800)
#define S_STRONG_PREFERRED (-600)
#define S_PREFERRED (-400)
#define S_STRONG_DEFAULT (-200)
#define S_DEFAULT 0
#define S_WEAK_DEFAULT 500
#define S_ABSOLUTE_WEAKEST 10000
/* constraint kinds */
#define K_EDIT 1
#define K_STAY 2
#define K_EQUAL 3
#define K_SCALE 4

/* --- bump arena (reset per test) --- */
#define ARENA_BYTES (4 * 1024 * 1024)
static char arena[ARENA_BYTES];
static long arena_top;
static int arena_overflow;

static void arena_reset(void) { arena_top = 0; arena_overflow = 0; }

static void *arena_alloc(long bytes) {
    void *p;
    bytes = (bytes + 7) & ~7L;
    if (arena_top + bytes > ARENA_BYTES) {
        arena_overflow = 1;
        arena_top = 0; /* keep going so the failure is reported, not a crash */
    }
    p = (void *) &arena[arena_top];
    arena_top += bytes;
    return p;
}

/* --- growable pointer vector (mirrors the .ls push/len/splice built-ins) --- */
typedef struct Vec {
    void **items;
    int size;
    int cap;
} Vec;

static Vec *vec_new(void) {
    Vec *v = (Vec *) arena_alloc(sizeof(Vec));
    v->size = 0;
    v->cap = 4;
    v->items = (void **) arena_alloc(v->cap * (long) sizeof(void *));
    return v;
}

static void vec_add(Vec *v, void *item) {
    if (v->size == v->cap) {
        void **grown = (void **) arena_alloc(2 * v->cap * (long) sizeof(void *));
        int i;
        for (i = 0; i < v->size; i++) grown[i] = v->items[i];
        v->items = grown;
        v->cap = 2 * v->cap;
    }
    v->items[v->size++] = item;
}

static void *vec_remove_first(Vec *v) {
    void *first;
    int i;
    if (v->size == 0) return 0;
    first = v->items[0];
    for (i = 1; i < v->size; i++) v->items[i - 1] = v->items[i];
    v->size--;
    return first;
}

static Vec *vec_with(void *item) {
    Vec *v = vec_new();
    vec_add(v, item);
    return v;
}

/* --- strength helpers --- */
static int s_stronger(int a, int b) { return a < b; }
static int s_weaker(int a, int b) { return a > b; }
static int s_weakest(int a, int b) { return a > b ? a : b; }

/* --- variable --- */
typedef struct Variable {
    int val;
    Vec *constraints;
    int determined_by; /* cid, 0 = none */
    int walk_strength;
    int stay;
    int mark;
} Variable;

static Variable *var_new(void) {
    Variable *v = (Variable *) arena_alloc(sizeof(Variable));
    v->val = 0;
    v->constraints = vec_new();
    v->determined_by = 0;
    v->walk_strength = S_ABSOLUTE_WEAKEST;
    v->stay = 1;
    v->mark = 0;
    return v;
}

static Variable *var_value(int a_value) {
    Variable *v = var_new();
    v->val = a_value;
    return v;
}

/* --- constraint: one tagged record, switch dispatch on kind --- */
typedef struct Constraint {
    int cid;
    int kind;
    int strength;
    Variable *out;       /* unary (edit/stay) */
    int satisfied;       /* unary */
    Variable *v1;        /* binary */
    Variable *v2;
    int direction;       /* binary: 0 = unsatisfied */
    Variable *sc;        /* scale only */
    Variable *off;
} Constraint;

/* remove constraint with cid from vector (first match, like the .ls) */
static int vec_remove_cid(Vec *v, int cid) {
    int found = -1;
    int i;
    for (i = 0; i < v->size; i++) {
        if (found == -1 && ((Constraint *) v->items[i])->cid == cid) found = i;
    }
    if (found == -1) return 0;
    for (i = found + 1; i < v->size; i++) v->items[i - 1] = v->items[i];
    v->size--;
    return 1;
}

static void var_add_constraint(Variable *variable, Constraint *c) {
    vec_add(variable->constraints, c);
}

static void var_remove_constraint(Variable *variable, Constraint *c) {
    vec_remove_cid(variable->constraints, c->cid);
    if (variable->determined_by == c->cid) variable->determined_by = 0;
}

/* --- planner --- */
typedef struct Planner {
    int current_mark;
    int next_cid;
} Planner;

static int planner_new_mark(Planner *planner) {
    planner->current_mark = planner->current_mark + 1;
    return planner->current_mark;
}

static int planner_next_cid(Planner *planner) {
    int nc = planner->next_cid;
    planner->next_cid = nc + 1;
    return nc;
}

/* --- constraint dispatch --- */

static int c_is_input(Constraint *c) { return c->kind == K_EDIT; }

static int c_is_satisfied(Constraint *c) {
    if (c->kind == K_EDIT || c->kind == K_STAY) return c->satisfied;
    return c->direction != 0;
}

static void c_add_to_graph(Constraint *c) {
    switch (c->kind) {
    case K_EDIT:
    case K_STAY:
        var_add_constraint(c->out, c);
        c->satisfied = 0;
        break;
    case K_EQUAL:
        var_add_constraint(c->v1, c);
        var_add_constraint(c->v2, c);
        c->direction = 0;
        break;
    case K_SCALE:
        var_add_constraint(c->v1, c);
        var_add_constraint(c->v2, c);
        var_add_constraint(c->sc, c);
        var_add_constraint(c->off, c);
        c->direction = 0;
        break;
    }
}

static void c_remove_from_graph(Constraint *c) {
    switch (c->kind) {
    case K_EDIT:
    case K_STAY:
        if (c->out != 0) var_remove_constraint(c->out, c);
        c->satisfied = 0;
        break;
    case K_EQUAL:
        if (c->v1 != 0) var_remove_constraint(c->v1, c);
        if (c->v2 != 0) var_remove_constraint(c->v2, c);
        c->direction = 0;
        break;
    case K_SCALE:
        if (c->v1 != 0) var_remove_constraint(c->v1, c);
        if (c->v2 != 0) var_remove_constraint(c->v2, c);
        if (c->sc != 0) var_remove_constraint(c->sc, c);
        if (c->off != 0) var_remove_constraint(c->off, c);
        c->direction = 0;
        break;
    }
}

static void c_choose_method(Constraint *c, int mark) {
    if (c->kind == K_EDIT || c->kind == K_STAY) {
        Variable *o = c->out;
        if (o->mark != mark && s_stronger(c->strength, o->walk_strength)) {
            c->satisfied = 1;
            return;
        }
        c->satisfied = 0;
        return;
    }
    /* binary / scale */
    {
        Variable *v1 = c->v1;
        Variable *v2 = c->v2;
        if (v1->mark == mark) {
            if (v2->mark != mark && s_stronger(c->strength, v2->walk_strength)) {
                c->direction = FORWARD;
                return;
            }
            c->direction = 0;
            return;
        }
        if (v2->mark == mark) {
            if (v1->mark != mark && s_stronger(c->strength, v1->walk_strength)) {
                c->direction = BACKWARD;
                return;
            }
            c->direction = 0;
            return;
        }
        /* neither marked */
        if (s_weaker(v1->walk_strength, v2->walk_strength)) {
            if (s_stronger(c->strength, v1->walk_strength)) {
                c->direction = BACKWARD;
                return;
            }
            c->direction = 0;
            return;
        }
        if (s_stronger(c->strength, v2->walk_strength)) {
            c->direction = FORWARD;
            return;
        }
        c->direction = 0;
    }
}

static void c_mark_unsatisfied(Constraint *c) {
    if (c->kind == K_EDIT || c->kind == K_STAY) c->satisfied = 0;
    else c->direction = 0;
}

static Variable *c_get_output(Constraint *c) {
    if (c->kind == K_EDIT || c->kind == K_STAY) return c->out;
    return c->direction == FORWARD ? c->v2 : c->v1;
}

static void c_mark_inputs(Constraint *c, int mark) {
    switch (c->kind) {
    case K_EDIT:
    case K_STAY:
        break; /* unary: no inputs */
    case K_EQUAL:
        if (c->direction == FORWARD) c->v1->mark = mark;
        if (c->direction == BACKWARD) c->v2->mark = mark;
        break;
    case K_SCALE:
        if (c->direction == FORWARD) c->v1->mark = mark;
        if (c->direction == BACKWARD) c->v2->mark = mark;
        c->sc->mark = mark;
        c->off->mark = mark;
        break;
    }
}

/* one input known: mark==mark || stay || determinedBy==0 */
static int input_known(Variable *inp, int mark) {
    if (inp->mark == mark) return 1;
    if (inp->stay != 0) return 1;
    if (inp->determined_by == 0) return 1;
    return 0;
}

static int c_inputs_known(Constraint *c, int mark) {
    Variable *inp;
    if (c->kind == K_EDIT || c->kind == K_STAY) return 1;
    inp = 0;
    if (c->direction == FORWARD) inp = c->v1;
    if (c->direction == BACKWARD) inp = c->v2;
    if (inp != 0 && !input_known(inp, mark)) return 0;
    if (c->kind == K_SCALE) {
        if (!input_known(c->sc, mark)) return 0;
        if (!input_known(c->off, mark)) return 0;
    }
    return 1;
}

static void c_execute(Constraint *c) {
    switch (c->kind) {
    case K_EDIT:
    case K_STAY:
        break; /* no-op */
    case K_EQUAL:
        if (c->direction == FORWARD) c->v2->val = c->v1->val;
        if (c->direction == BACKWARD) c->v1->val = c->v2->val;
        break;
    case K_SCALE:
        if (c->direction == FORWARD) c->v2->val = c->v1->val * c->sc->val + c->off->val;
        if (c->direction == BACKWARD) c->v1->val = (c->v2->val - c->off->val) / c->sc->val;
        break;
    }
}

static void c_recalculate(Constraint *c) {
    Variable *ihn;
    Variable *out;
    switch (c->kind) {
    case K_EDIT:
        c->out->walk_strength = c->strength;
        c->out->stay = 0; /* isInput = true */
        break;
    case K_STAY:
        c->out->walk_strength = c->strength;
        c->out->stay = 1; /* isInput = false; stay execute is a no-op */
        break;
    case K_EQUAL:
        if (c->direction == FORWARD) { ihn = c->v1; out = c->v2; }
        else { ihn = c->v2; out = c->v1; }
        out->walk_strength = s_weakest(c->strength, ihn->walk_strength);
        out->stay = ihn->stay;
        if (out->stay == 1) c_execute(c);
        break;
    case K_SCALE:
        if (c->direction == FORWARD) { ihn = c->v1; out = c->v2; }
        else { ihn = c->v2; out = c->v1; }
        out->walk_strength = s_weakest(c->strength, ihn->walk_strength);
        out->stay = (ihn->stay != 0 && c->sc->stay != 0 && c->off->stay != 0) ? 1 : 0;
        if (out->stay == 1) c_execute(c);
        break;
    }
}

/* --- planner methods --- */

static void planner_incremental_add(Planner *planner, Constraint *c);
static void planner_incremental_remove(Planner *planner, Constraint *c);

static void planner_add_constraints_consuming_to(Variable *v, Vec *coll) {
    int det = v->determined_by;
    Vec *cs = v->constraints;
    int i;
    for (i = 0; i < cs->size; i++) {
        Constraint *c = (Constraint *) cs->items[i];
        if (c->cid != det && c_is_satisfied(c)) vec_add(coll, c);
    }
}

static int planner_add_propagate(Planner *planner, Constraint *c, int mark) {
    Vec *todo = vec_with(c);
    while (todo->size != 0) {
        Constraint *d = (Constraint *) vec_remove_first(todo);
        Variable *out = c_get_output(d);
        if (out->mark == mark) {
            planner_incremental_remove(planner, c);
            return 0;
        }
        c_recalculate(d);
        planner_add_constraints_consuming_to(out, todo);
    }
    return 1;
}

/* returns the overridden constraint (or 0) */
static Constraint *c_satisfy(Constraint *c, int mark, Planner *planner) {
    c_choose_method(c, mark);
    if (c_is_satisfied(c)) {
        Variable *out;
        Constraint *overridden = 0;
        c_mark_inputs(c, mark);
        out = c_get_output(c);
        if (out->determined_by != 0) {
            /* find the old constraint by cid */
            Vec *cs = out->constraints;
            int i;
            for (i = 0; i < cs->size; i++) {
                Constraint *cc = (Constraint *) cs->items[i];
                if (cc->cid == out->determined_by) {
                    c_mark_unsatisfied(cc);
                    overridden = cc;
                }
            }
        }
        out->determined_by = c->cid;
        if (planner_add_propagate(planner, c, mark) == 0) printf("ERROR: cycle\n");
        out->mark = mark;
        return overridden;
    }
    if (c->strength == S_REQUIRED) printf("ERROR: required constraint not satisfied\n");
    return 0;
}

static void planner_incremental_add(Planner *planner, Constraint *c) {
    int mark = planner_new_mark(planner);
    Constraint *overridden = c_satisfy(c, mark, planner);
    while (overridden != 0) overridden = c_satisfy(overridden, mark, planner);
}

static Vec *planner_remove_propagate_from(Variable *out) {
    Vec *unsatisfied = vec_new();
    Vec *todo;
    out->determined_by = 0;
    out->walk_strength = S_ABSOLUTE_WEAKEST;
    out->stay = 1;
    todo = vec_with(out);
    while (todo->size != 0) {
        Variable *v = (Variable *) vec_remove_first(todo);
        Vec *cs = v->constraints;
        int csz = cs->size;
        int det = v->determined_by;
        int i;
        for (i = 0; i < csz; i++) {
            Constraint *cc = (Constraint *) cs->items[i];
            if (!c_is_satisfied(cc)) vec_add(unsatisfied, cc);
        }
        for (i = 0; i < csz; i++) {
            Constraint *cc2 = (Constraint *) cs->items[i];
            if (cc2->cid != det && c_is_satisfied(cc2)) {
                c_recalculate(cc2);
                vec_add(todo, c_get_output(cc2));
            }
        }
    }
    /* sort skipped — not needed for correctness (matches the .ls) */
    return unsatisfied;
}

static void planner_incremental_remove(Planner *planner, Constraint *c) {
    Variable *out = c_get_output(c);
    Vec *unsatisfied;
    int i;
    c_mark_unsatisfied(c);
    c_remove_from_graph(c);
    unsatisfied = planner_remove_propagate_from(out);
    for (i = 0; i < unsatisfied->size; i++) {
        planner_incremental_add(planner, (Constraint *) unsatisfied->items[i]);
    }
}

static Vec *planner_make_plan(Planner *planner, Vec *sources) {
    int mark = planner_new_mark(planner);
    Vec *plan = vec_new();
    Vec *todo = sources;
    while (todo->size != 0) {
        Constraint *c = (Constraint *) vec_remove_first(todo);
        Variable *out = c_get_output(c);
        if (out->mark != mark && c_inputs_known(c, mark)) {
            vec_add(plan, c);
            out->mark = mark;
            planner_add_constraints_consuming_to(out, todo);
        }
    }
    return plan;
}

static Vec *planner_extract_plan(Planner *planner, Vec *constraints) {
    Vec *sources = vec_new();
    int i;
    for (i = 0; i < constraints->size; i++) {
        Constraint *c = (Constraint *) constraints->items[i];
        if (c_is_input(c) && c_is_satisfied(c)) vec_add(sources, c);
    }
    return planner_make_plan(planner, sources);
}

/* --- constraint constructors --- */

static void c_add_constraint(Constraint *c, Planner *planner) {
    c_add_to_graph(c);
    planner_incremental_add(planner, c);
}

static void c_destroy_constraint(Constraint *c, Planner *planner) {
    if (c_is_satisfied(c)) planner_incremental_remove(planner, c);
    c_remove_from_graph(c);
}

static Constraint *constraint_alloc(int kind, int strength, Planner *planner) {
    Constraint *c = (Constraint *) arena_alloc(sizeof(Constraint));
    c->cid = planner_next_cid(planner);
    c->kind = kind;
    c->strength = strength;
    c->out = 0;
    c->satisfied = 0;
    c->v1 = 0;
    c->v2 = 0;
    c->direction = 0;
    c->sc = 0;
    c->off = 0;
    return c;
}

static Constraint *edit_constraint_new(Variable *v, int strength, Planner *planner) {
    Constraint *c = constraint_alloc(K_EDIT, strength, planner);
    c->out = v;
    c_add_constraint(c, planner);
    return c;
}

static Constraint *stay_constraint_new(Variable *v, int strength, Planner *planner) {
    Constraint *c = constraint_alloc(K_STAY, strength, planner);
    c->out = v;
    c_add_constraint(c, planner);
    return c;
}

static Constraint *equality_constraint_new(Variable *v1, Variable *v2, int strength, Planner *planner) {
    Constraint *c = constraint_alloc(K_EQUAL, strength, planner);
    c->v1 = v1;
    c->v2 = v2;
    c_add_constraint(c, planner);
    return c;
}

static Constraint *scale_constraint_new(Variable *src, Variable *scale, Variable *offset,
                                        Variable *dest, int strength, Planner *planner) {
    Constraint *c = constraint_alloc(K_SCALE, strength, planner);
    c->v1 = src;
    c->v2 = dest;
    c->sc = scale;
    c->off = offset;
    c_add_constraint(c, planner);
    return c;
}

static void planner_change(Planner *planner, Variable *v, int new_value) {
    Constraint *edit_c = edit_constraint_new(v, S_PREFERRED, planner);
    Vec *edit_v = vec_with(edit_c);
    Vec *plan = planner_extract_plan(planner, edit_v);
    int i;
    for (i = 0; i < 10; i++) {
        int j;
        v->val = new_value;
        for (j = 0; j < plan->size; j++) c_execute((Constraint *) plan->items[j]);
    }
    c_destroy_constraint(edit_c, planner);
}

/* --- benchmark tests --- */

static int chain_test(int n) {
    Planner planner;
    Vec *vars;
    Variable *tail_var;
    Variable *first;
    Constraint *edit_c;
    Vec *plan;
    int i, k;

    arena_reset();
    planner.current_mark = 1;
    planner.next_cid = 1;

    vars = vec_new();
    for (i = 0; i < n + 1; i++) vec_add(vars, var_new());
    for (i = 0; i < n; i++) {
        equality_constraint_new((Variable *) vars->items[i], (Variable *) vars->items[i + 1],
                                S_REQUIRED, &planner);
    }
    tail_var = (Variable *) vars->items[n];
    stay_constraint_new(tail_var, S_STRONG_DEFAULT, &planner);
    first = (Variable *) vars->items[0];
    edit_c = edit_constraint_new(first, S_PREFERRED, &planner);
    plan = planner_extract_plan(&planner, vec_with(edit_c));
    for (k = 0; k < 100; k++) {
        int m;
        first->val = k;
        for (m = 0; m < plan->size; m++) c_execute((Constraint *) plan->items[m]);
        if (tail_var->val != k) {
            printf("Chain test FAILED at k=%d lastval=%d\n", k, tail_var->val);
            return 0;
        }
    }
    c_destroy_constraint(edit_c, &planner);
    if (arena_overflow) {
        printf("Chain test FAILED: arena overflow\n");
        return 0;
    }
    return 1;
}

static int projection_test(int n) {
    Planner planner;
    Vec *dests;
    Variable *scale;
    Variable *offset;
    Variable *src = 0;
    Variable *dst = 0;
    int i, j, k;

    arena_reset();
    planner.current_mark = 1;
    planner.next_cid = 1;

    dests = vec_new();
    scale = var_value(10);
    offset = var_value(1000);
    for (i = 1; i <= n; i++) {
        src = var_value(i);
        dst = var_value(i);
        vec_add(dests, dst);
        stay_constraint_new(src, S_DEFAULT, &planner);
        scale_constraint_new(src, scale, offset, dst, S_REQUIRED, &planner);
    }
    planner_change(&planner, src, 17);
    if (dst->val != 1170) {
        printf("Projection test 1 FAILED: dst=%d\n", dst->val);
        return 0;
    }
    planner_change(&planner, dst, 1050);
    if (src->val != 5) {
        printf("Projection test 2 FAILED: src=%d\n", src->val);
        return 0;
    }
    planner_change(&planner, scale, 5);
    for (j = 0; j < n - 1; j++) {
        Variable *dj = (Variable *) dests->items[j];
        int expected = (j + 1) * 5 + 1000;
        if (dj->val != expected) {
            printf("Projection test 3 FAILED at j=%d got=%d expected=%d\n", j, dj->val, expected);
            return 0;
        }
    }
    planner_change(&planner, offset, 2000);
    for (k = 0; k < n - 1; k++) {
        Variable *dk = (Variable *) dests->items[k];
        int expected2 = (k + 1) * 5 + 2000;
        if (dk->val != expected2) {
            printf("Projection test 4 FAILED at k=%d got=%d expected=%d\n", k, dk->val, expected2);
            return 0;
        }
    }
    if (arena_overflow) {
        printf("Projection test FAILED: arena overflow\n");
        return 0;
    }
    return 1;
}

int main(void) {
    /* 20 outer iterations of the 100-element tests: the canonical AWFY
     * workload, matching deltablue.ls (runAWFY('DeltaBlue', ..., 100, 20)) */
    int k;
    for (k = 0; k < 20; k++) {
        if (chain_test(100) == 0) {
            printf("DeltaBlue: FAIL (chain)\n");
            return 1;
        }
        if (projection_test(100) == 0) {
            printf("DeltaBlue: FAIL (projection)\n");
            return 1;
        }
    }
    printf("DeltaBlue: PASS\n");
    return 0;
}
