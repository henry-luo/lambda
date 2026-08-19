/* Native C2MIR port of larceny/deriv.ls. */
/* Symbolic differentiation of 3*x^3 + 2*x^2 + x + 5, repeated 5000 times.
 * Expression nodes are a tagged struct (0=const, 1=var, 2=add, 3=mul),
 * heap-allocated per iteration like the .ls maps. The derivative shares
 * subtrees with the input expression (product rule reuses e->l / e->r), so
 * the nodes are intentionally leaked instead of freed -- ownership of shared
 * subtrees has no single owner, and the .ls relies on GC for the same reason.
 * Total leak is bounded (~5000 * 61 nodes). */
extern int printf(const char *, ...);
extern void *malloc(unsigned long);

typedef struct Expr {
    int t;             /* 0=const, 1=var(x), 2=add, 3=mul */
    int v;             /* constant value (t==0) */
    struct Expr *l;
    struct Expr *r;
} Expr;

static Expr *mk(int t, int v, Expr *l, Expr *r) {
    Expr *e = (Expr *) malloc(sizeof(Expr));
    e->t = t;
    e->v = v;
    e->l = l;
    e->r = r;
    return e;
}

static Expr *deriv(Expr *e) {
    Expr *dl;
    Expr *dr;
    if (e->t == 0) return mk(0, 0, 0, 0);
    if (e->t == 1) return mk(0, 1, 0, 0);
    if (e->t == 2) {
        dl = deriv(e->l);
        dr = deriv(e->r);
        return mk(2, 0, dl, dr);
    }
    /* t == 3: product rule  d(a*b) = a*db + da*b */
    dl = deriv(e->l);
    dr = deriv(e->r);
    return mk(2, 0, mk(3, 0, e->l, dr), mk(3, 0, dl, e->r));
}

static int count_nodes(const Expr *e) {
    if (e->t == 0 || e->t == 1) return 1;
    return 1 + count_nodes(e->l) + count_nodes(e->r);
}

static Expr *make_expr(void) {
    /* 3*x*x*x + 2*x*x + x + 5 */
    Expr *c3 = mk(0, 3, 0, 0);
    Expr *c2 = mk(0, 2, 0, 0);
    Expr *c5 = mk(0, 5, 0, 0);
    Expr *x1 = mk(1, 0, 0, 0);
    Expr *x2 = mk(1, 0, 0, 0);
    Expr *x3 = mk(1, 0, 0, 0);
    Expr *x4 = mk(1, 0, 0, 0);
    Expr *x5 = mk(1, 0, 0, 0);
    Expr *x6 = mk(1, 0, 0, 0);
    Expr *m1 = mk(3, 0, c3, x1);
    Expr *m2 = mk(3, 0, m1, x2);
    Expr *m3 = mk(3, 0, m2, x3);
    Expr *m4 = mk(3, 0, c2, x4);
    Expr *m5 = mk(3, 0, m4, x5);
    Expr *a1 = mk(2, 0, m3, m5);
    Expr *a2 = mk(2, 0, a1, x6);
    return mk(2, 0, a2, c5);
}

static int benchmark(void) {
    int result = 0;
    int iter;
    for (iter = 0; iter < 5000; iter++) {
        Expr *e = make_expr();
        Expr *d = deriv(e);
        result = count_nodes(d);
    }
    return result;
}

int main(void) {
    int result = benchmark();
    if (result == 45) {
        printf("deriv: PASS\n");
    } else {
        printf("deriv: FAIL result=%d\n", result);
    }
    return result != 45;
}
