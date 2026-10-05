/* jq-core: a reduced jq interpreter for the C2MIR column of the jq_* text
 * benchmark rows (vibe/impl/Lambda_Impl_Jq_Tests.md).
 *
 * It is modelled on jq 1.7.1's own implementation (bytecode compiler plus
 * backtracking VM over a forkable stack; builtins from src/builtin.jq) and
 * covers the subset the jq_*.jq filters use. Values are immutable and
 * garbage-collected, so updates copy along their path; the typed Lambda port
 * of this VM (jq_vm.ls) has the same design. The VM runs the same filter text
 * as every other column of the row.
 *
 *   jq_value.h  values, strings/arrays/objects, total order, JSON text
 *   jq_parse.h  jq source -> AST
 *   jq_vm.h     AST -> bytecode, prelude, natives table
 *   jq_core.h   frames, forkable stack, VM loop, natives, GC, harness
 */
#include "jq_value.h"
#include "jq_parse.h"
#include "jq_vm.h"

typedef struct Frame Frame;

typedef struct {
    int fn;
    Frame *env;
} Closure;

struct Frame {
    GcHdr h;
    Frame *env;          /* frame of the lexically enclosing function */
    Frame *caller;
    int retpc;
    int fn;
    int fork_base;       /* fork records at entry: tail calls need none above */
    int nparams;
    int nlocals;
    unsigned long size;
    Closure *params;
    V *locals;
};

enum { FK_FORK = 1, FK_TRY, FK_TRY_EXIT, FK_EACH, FK_RANGE };

typedef struct {
    int kind;
    int pc;
    int top, limit;
    Frame *fp;
    V path, vat;
    int subexp;
    V aux, aux2, aux3;   /* EACH: container; RANGE: next, upto, step */
    int auxi;            /* EACH: next index; TRY_EXIT: the try record */
    int active;          /* TRY: catches errors while its body runs */
    long long label;     /* TRY: label id for `label $x`, 0 for try */
} Fork;

/* forkable data stack */
static V *stv = 0;
static int *stp = 0;
static int stcap = 0;
static int sttop = -1, stlimit = -1, sthigh = -1;

static Fork *forks = 0;
static int nforks = 0, capforks = 0;

static Frame *fp = 0;
static V cur_path = V_INVALID, cur_vat = V_NULL;
static int subexp = 0;
static long long label_counter = 0;

static V *outputs = 0;
static int noutputs = 0, capoutputs = 0;

static void st_push(V v) {
    int idx = (sttop > stlimit ? sttop : stlimit) + 1;
    if (idx >= stcap) {
        stcap = stcap ? stcap * 2 : 4096;
        stv = (V *) realloc(stv, sizeof(V) * (unsigned long) stcap);
        stp = (int *) realloc(stp, sizeof(int) * (unsigned long) stcap);
    }
    stv[idx] = v;
    stp[idx] = sttop;
    sttop = idx;
    if (idx > sthigh) sthigh = idx;
}

static V st_pop(void) {
    V v = stv[sttop];
    sttop = stp[sttop];
    return v;
}

static Fork *fork_push(int kind, int pc) {
    if (nforks == capforks) {
        capforks = capforks ? capforks * 2 : 1024;
        forks = (Fork *) realloc(forks, sizeof(Fork) * (unsigned long) capforks);
    }
    Fork *f = &forks[nforks++];
    f->kind = kind;
    f->pc = pc;
    f->top = sttop;
    f->limit = stlimit;
    f->fp = fp;
    f->path = cur_path;
    f->vat = cur_vat;
    f->subexp = subexp;
    f->aux = f->aux2 = f->aux3 = V_NULL;
    f->auxi = 0;
    f->active = 1;
    f->label = 0;
    /* protect every live cell: later pushes go above them */
    if (sttop > stlimit) stlimit = sttop;
    return f;
}

static void fork_restore(Fork *f) {
    sttop = f->top;
    stlimit = f->limit;
    sthigh = sttop > stlimit ? sttop : stlimit;
    fp = f->fp;
    cur_path = f->path;
    cur_vat = f->vat;
    subexp = f->subexp;
}

static Frame *frame_new(int fn, Frame *env, Frame *caller, int retpc) {
    int np = prog.fns[fn].nparams, nl = prog.fns[fn].nlocals;
    unsigned long size = sizeof(Frame) + sizeof(Closure) * (unsigned long) np + sizeof(V) * (unsigned long) nl;
    Frame *f = (Frame *) gc_alloc(size, K_FRAME);
    f->env = env;
    f->caller = caller;
    f->retpc = retpc;
    f->fn = fn;
    f->fork_base = nforks;
    f->nparams = np;
    f->nlocals = nl;
    f->size = size;
    f->params = (Closure *) (f + 1);
    f->locals = (V *) (f->params + np);
    for (int i = 0; i < nl; i++) f->locals[i] = V_NULL;
    return f;
}

static Frame *frame_hop(Frame *f, int hops) {
    while (hops-- > 0) f = f->env;
    return f;
}

/* ---------- GC ---------- */

static GcHdr **gc_work = 0;
static int gc_nwork = 0, gc_capwork = 0;
static unsigned long gc_live = 0;

static void gc_mark_hdr(GcHdr *h) {
    if (!h || h->mark) return;
    h->mark = 1;
    if (gc_nwork == gc_capwork) {
        gc_capwork = gc_capwork ? gc_capwork * 2 : 4096;
        gc_work = (GcHdr **) realloc(gc_work, sizeof(GcHdr *) * (unsigned long) gc_capwork);
    }
    gc_work[gc_nwork++] = h;
}

static void gc_mark_value(V v) {
    if (v_is_ptr(v)) gc_mark_hdr((GcHdr *) v_heap(v));
}

static void gc_drain(void) {
    while (gc_nwork > 0) {
        GcHdr *h = gc_work[--gc_nwork];
        switch (h->kind) {
        case K_STRING:
            gc_live += sizeof(Str) + (unsigned long) ((Str *) h)->len;
            break;
        case K_ARRAY: {
            Arr *a = (Arr *) h;
            gc_live += sizeof(Arr) + sizeof(V) * (unsigned long) a->cap;
            for (int i = 0; i < a->len; i++) gc_mark_value(a->items[i]);
            break;
        }
        case K_OBJECT: {
            Obj *o = (Obj *) h;
            gc_live += sizeof(Obj) + 2 * sizeof(V) * (unsigned long) o->cap;
            for (int i = 0; i < o->len; i++) {
                gc_mark_value(o->keys[i]);
                gc_mark_value(o->vals[i]);
            }
            break;
        }
        case K_FRAME: {
            Frame *f = (Frame *) h;
            gc_live += f->size;
            gc_mark_hdr((GcHdr *) f->env);
            gc_mark_hdr((GcHdr *) f->caller);
            for (int i = 0; i < f->nparams; i++) gc_mark_hdr((GcHdr *) f->params[i].env);
            for (int i = 0; i < f->nlocals; i++) gc_mark_value(f->locals[i]);
            break;
        }
        }
    }
}

/* runs only at the VM's safe point, where every live value is a root below */
static void gc_collect(void) {
    gc_live = 0;
    for (int i = 0; i <= sthigh; i++) gc_mark_value(stv[i]);
    for (int i = 0; i < nforks; i++) {
        Fork *f = &forks[i];
        gc_mark_hdr((GcHdr *) f->fp);
        gc_mark_value(f->path);
        gc_mark_value(f->vat);
        gc_mark_value(f->aux);
        gc_mark_value(f->aux2);
        gc_mark_value(f->aux3);
    }
    gc_mark_hdr((GcHdr *) fp);
    gc_mark_value(cur_path);
    gc_mark_value(cur_vat);
    gc_mark_value(jq_err_val);
    for (int i = 0; i < prog.nconsts; i++) gc_mark_value(prog.consts[i]);
    for (int i = 0; i < noutputs; i++) gc_mark_value(outputs[i]);
    gc_drain();
    GcHdr **link = &gc_objects;
    while (*link) {
        GcHdr *h = *link;
        if (h->mark) {
            h->mark = 0;
            link = &h->next;
            continue;
        }
        *link = h->next;
        if (h->kind == K_ARRAY) free(((Arr *) h)->items);
        else if (h->kind == K_OBJECT) {
            free(((Obj *) h)->keys);
            free(((Obj *) h)->vals);
        }
        free(h);
    }
    gc_bytes = gc_live;
    gc_threshold = gc_live * 2 > JQ_GC_MIN_THRESHOLD ? gc_live * 2 : JQ_GC_MIN_THRESHOLD;
}

/* ---------- value helpers for natives ---------- */

static V describe(V v) {
    Buf b = {0, 0, 0};
    V text = v_tojson(v);
    Str *s = v_str(text);
    buf_adds(&b, type_name(v));
    buf_adds(&b, " (");
    if (s->len > 11) {
        buf_add(&b, s->data, 10);
        buf_adds(&b, "...");
    } else {
        buf_add(&b, s->data, s->len);
    }
    buf_adds(&b, ")");
    return buf_to_str(&b);
}

static void raise2(V a, const char *mid, V b, const char *tail) {
    Buf buf = {0, 0, 0};
    Str *x = v_str(describe(a));
    buf_add(&buf, x->data, x->len);
    buf_adds(&buf, mid);
    if (b != V_INVALID) {
        Str *y = v_str(describe(b));
        buf_add(&buf, y->data, y->len);
    }
    buf_adds(&buf, tail);
    raise_value(buf_to_str(&buf));
}

static V index_value(V t, V k) {
    int tt = v_type(t), kt = v_type(k);
    if (tt == T_NULL && (kt == T_STRING || kt == T_NUMBER || kt == T_NULL)) return V_NULL;
    if (tt == T_OBJECT && kt == T_STRING) return obj_get(t, k);
    if (tt == T_ARRAY && kt == T_NUMBER) {
        Arr *a = v_arr(t);
        double d = floor(v_dbl(k));
        if (d < 0) d += a->len;
        if (d < 0 || d >= a->len) return V_NULL;
        return a->items[(int) d];
    }
    Buf b = {0, 0, 0};
    buf_adds(&b, "Cannot index ");
    buf_adds(&b, type_name(t));
    buf_adds(&b, " with ");
    if (kt == T_STRING) json_string_to_buf(&b, k);
    else buf_adds(&b, type_name(k));
    raise_value(buf_to_str(&b));
    return V_NULL;
}

static V slice_value(V t, V from, V to) {
    int tt = v_type(t);
    if (tt == T_NULL) return V_NULL;
    if ((from != V_NULL && !v_is_num(from)) || (to != V_NULL && !v_is_num(to))) {
        raise_msg("Start and end indices of an array slice must be numbers");
        return V_NULL;
    }
    int len;
    if (tt == T_STRING) len = str_codepoints(v_str(t));
    else if (tt == T_ARRAY) len = v_arr(t)->len;
    else { raise2(t, "cannot be sliced", V_INVALID, ""); return V_NULL; }
    double s = from == V_NULL ? 0 : v_dbl(from), e = to == V_NULL ? len : v_dbl(to);
    if (s < 0) s += len;
    if (e < 0) e += len;
    s = floor(s);
    e = ceil(e);
    if (s < 0) s = 0;
    if (e > len) e = len;
    if (s > len) s = len;
    if (e < s) e = s;
    int a = (int) s, z = (int) e;
    if (tt == T_STRING) {
        Str *str = v_str(t);
        int bs = str_byte_offset(str, a), be = str_byte_offset(str, z);
        return str_new(str->data + bs, be - bs);
    }
    V r = arr_new(z - a);
    Arr *src = v_arr(t), *dst = v_arr(r);
    for (int i = a; i < z; i++) dst->items[dst->len++] = src->items[i];
    return r;
}

static V binop_value(int op, V l, V r) {
    int lt = v_type(l), rt = v_type(r);
    switch (op) {
    case OPB_ADD:
        if (lt == T_NUMBER && rt == T_NUMBER) return v_num(v_dbl(l) + v_dbl(r));
        if (lt == T_NULL) return r;
        if (rt == T_NULL) return l;
        if (lt == T_STRING && rt == T_STRING) {
            Str *a = v_str(l), *b = v_str(r);
            Buf buf = {0, 0, 0};
            buf_reserve(&buf, a->len + b->len);
            buf_add(&buf, a->data, a->len);
            buf_add(&buf, b->data, b->len);
            return buf_to_str(&buf);
        }
        if (lt == T_ARRAY && rt == T_ARRAY) {
            V c = arr_copy(l, v_arr(r)->len);
            Arr *b = v_arr(r);
            for (int i = 0; i < b->len; i++) arr_push(c, b->items[i]);
            return c;
        }
        if (lt == T_OBJECT && rt == T_OBJECT) {
            V c = obj_copy(l, v_obj(r)->len);
            Obj *b = v_obj(r);
            for (int i = 0; i < b->len; i++) obj_put(c, b->keys[i], b->vals[i]);
            return c;
        }
        raise2(l, " and ", r, " cannot be added");
        return V_NULL;
    case OPB_SUB:
        if (lt == T_NUMBER && rt == T_NUMBER) return v_num(v_dbl(l) - v_dbl(r));
        if (lt == T_ARRAY && rt == T_ARRAY) {
            V c = arr_new(v_arr(l)->len);
            Arr *a = v_arr(l), *b = v_arr(r);
            for (int i = 0; i < a->len; i++) {
                int keep = 1;
                for (int j = 0; j < b->len && keep; j++) if (v_equal(a->items[i], b->items[j])) keep = 0;
                if (keep) arr_push(c, a->items[i]);
            }
            return c;
        }
        raise2(l, " and ", r, " cannot be subtracted");
        return V_NULL;
    case OPB_MUL:
        if (lt == T_NUMBER && rt == T_NUMBER) return v_num(v_dbl(l) * v_dbl(r));
        if ((lt == T_STRING && rt == T_NUMBER) || (lt == T_NUMBER && rt == T_STRING)) {
            V s = lt == T_STRING ? l : r;
            double n = v_dbl(lt == T_NUMBER ? l : r);
            if (n <= 0) return V_NULL;
            int times = (int) n;
            if (times < 1) times = 1;
            Str *x = v_str(s);
            Buf buf = {0, 0, 0};
            buf_reserve(&buf, x->len * times);
            for (int i = 0; i < times; i++) buf_add(&buf, x->data, x->len);
            return buf_to_str(&buf);
        }
        raise2(l, " and ", r, " cannot be multiplied");
        return V_NULL;
    case OPB_DIV:
        if (lt == T_NUMBER && rt == T_NUMBER) {
            if (v_dbl(r) == 0.0) {
                raise2(l, " and ", r, " cannot be divided because the divisor is zero");
                return V_NULL;
            }
            return v_num(v_dbl(l) / v_dbl(r));
        }
        raise2(l, " and ", r, " cannot be divided");
        return V_NULL;
    case OPB_MOD:
        if (lt == T_NUMBER && rt == T_NUMBER) {
            long long a = (long long) v_dbl(l), b = (long long) v_dbl(r);
            if (b == 0) {
                raise2(l, " and ", r, " cannot be divided because the divisor is zero");
                return V_NULL;
            }
            if (b < 0) b = -b;
            return v_num((double) (a % b));
        }
        raise2(l, " and ", r, " cannot be divided");
        return V_NULL;
    case OPB_EQ: return v_bool(v_equal(l, r));
    case OPB_NE: return v_bool(!v_equal(l, r));
    case OPB_LT: return v_bool(v_compare(l, r) < 0);
    case OPB_LE: return v_bool(v_compare(l, r) <= 0);
    case OPB_GT: return v_bool(v_compare(l, r) > 0);
    case OPB_GE: return v_bool(v_compare(l, r) >= 0);
    }
    return V_NULL;
}

/* jq's jv_contains: a type mismatch below the top level is just "false" */
static int contains_value(V a, V b) {
    int ta = v_type(a), tb = v_type(b);
    if (ta == T_OBJECT && tb == T_OBJECT) {
        Obj *o = v_obj(b);
        for (int i = 0; i < o->len; i++) {
            int at = obj_find(v_obj(a), o->keys[i]);
            if (at < 0 || !contains_value(v_obj(a)->vals[at], o->vals[i])) return 0;
        }
        return 1;
    }
    if (ta == T_ARRAY && tb == T_ARRAY) {
        Arr *x = v_arr(a), *y = v_arr(b);
        for (int j = 0; j < y->len; j++) {
            int found = 0;
            for (int i = 0; i < x->len && !found; i++) found = contains_value(x->items[i], y->items[j]);
            if (!found) return 0;
        }
        return 1;
    }
    if (ta == T_STRING && tb == T_STRING) {
        Str *x = v_str(a), *y = v_str(b);
        if (y->len == 0) return 1;
        for (int i = 0; i + y->len <= x->len; i++)
            if (memcmp(x->data + i, y->data, (unsigned long) y->len) == 0) return 1;
        return 0;
    }
    return v_equal(a, b);
}

static V getpath_value(V t, V p) {
    Arr *path = v_arr(p);
    for (int i = 0; i < path->len; i++) {
        if (t == V_NULL) return V_NULL;
        t = index_value(t, path->items[i]);
        if (jq_err_set) return V_NULL;
    }
    return t;
}

static V setpath_rec(V t, Arr *p, int i, V v) {
    if (i == p->len) return v;
    V k = p->items[i];
    if (v_type(k) == T_STRING) {
        if (t == V_NULL) t = obj_new(1);
        if (v_type(t) != T_OBJECT) { index_value(t, k); return V_NULL; }
        V child = setpath_rec(obj_get(t, k), p, i + 1, v);
        if (jq_err_set) return V_NULL;
        V c = obj_copy(t, 1);
        obj_put(c, k, child);
        return c;
    }
    if (v_type(k) == T_NUMBER) {
        if (t == V_NULL) t = arr_new(0);
        if (v_type(t) != T_ARRAY) { index_value(t, k); return V_NULL; }
        Arr *a = v_arr(t);
        double d = floor(v_dbl(k));
        if (d < 0) d += a->len;
        if (d < 0) { raise_msg("Out of bounds negative array index"); return V_NULL; }
        int idx = (int) d;
        V child = setpath_rec(idx < a->len ? a->items[idx] : V_NULL, p, i + 1, v);
        if (jq_err_set) return V_NULL;
        V c = arr_copy(t, idx + 1 > a->len ? idx + 1 - a->len : 0);
        Arr *b = v_arr(c);
        while (b->len <= idx) b->items[b->len++] = V_NULL;
        b->items[idx] = child;
        return c;
    }
    raise_msg("Invalid path component");
    return V_NULL;
}

static V delpath_rec(V t, Arr *p, int i) {
    if (t == V_NULL) return V_NULL;
    V k = p->items[i];
    int last = i == p->len - 1;
    if (v_type(t) == T_OBJECT && v_type(k) == T_STRING) {
        int at = obj_find(v_obj(t), k);
        if (at < 0) return t;
        if (last) return obj_without(t, at);
        V child = delpath_rec(v_obj(t)->vals[at], p, i + 1);
        if (jq_err_set) return V_NULL;
        V c = obj_copy(t, 0);
        v_obj(c)->vals[at] = child;
        return c;
    }
    if (v_type(t) == T_ARRAY && v_type(k) == T_NUMBER) {
        Arr *a = v_arr(t);
        double d = floor(v_dbl(k));
        if (d < 0) d += a->len;
        if (d < 0 || d >= a->len) return t;
        int idx = (int) d;
        if (last) {
            V c = arr_new(a->len - 1);
            Arr *b = v_arr(c);
            for (int j = 0; j < a->len; j++) if (j != idx) b->items[b->len++] = a->items[j];
            return c;
        }
        V child = delpath_rec(a->items[idx], p, i + 1);
        if (jq_err_set) return V_NULL;
        V c = arr_copy(t, 0);
        v_arr(c)->items[idx] = child;
        return c;
    }
    index_value(t, k);
    return V_NULL;
}

static V delpaths_value(V t, V ps) {
    if (v_type(ps) != T_ARRAY) { raise_msg("Paths must be specified as an array"); return V_NULL; }
    V sorted = arr_copy(ps, 0);
    sort_values(v_arr(sorted)->items, v_arr(sorted)->len);
    Arr *s = v_arr(sorted);
    for (int i = s->len - 1; i >= 0; i--) {
        V p = s->items[i];
        if (v_type(p) != T_ARRAY) { raise_msg("Path must be specified as an array"); return V_NULL; }
        if (v_arr(p)->len == 0) return V_NULL;
        t = delpath_rec(t, v_arr(p), 0);
        if (jq_err_set) return V_NULL;
    }
    return t;
}

static void flatten_into(V out, V a) {
    Arr *x = v_arr(a);
    for (int i = 0; i < x->len; i++) {
        if (v_type(x->items[i]) == T_ARRAY) flatten_into(out, x->items[i]);
        else arr_push(out, x->items[i]);
    }
}

/* stable sort of `n` element indices by their keys */
static void sort_indices(int *idx, int *tmp, V *keys, int n) {
    if (n < 2) return;
    int h = n / 2;
    sort_indices(idx, tmp, keys, h);
    sort_indices(idx + h, tmp, keys, n - h);
    int i = 0, j = h, k = 0;
    while (i < h && j < n) tmp[k++] = v_compare(keys[idx[j]], keys[idx[i]]) < 0 ? idx[j++] : idx[i++];
    while (i < h) tmp[k++] = idx[i++];
    while (j < n) tmp[k++] = idx[j++];
    memcpy(idx, tmp, sizeof(int) * (unsigned long) n);
}

static int *sorted_order(V input, V keys) {
    int n = v_arr(input)->len;
    int *idx = (int *) malloc(sizeof(int) * (unsigned long) (n + 1));
    int *tmp = (int *) malloc(sizeof(int) * (unsigned long) (n + 1));
    for (int i = 0; i < n; i++) idx[i] = i;
    sort_indices(idx, tmp, v_arr(keys)->items, n);
    free(tmp);
    return idx;
}

static V call_native(int id, V in, V *args) {
    int t = v_type(in);
    switch (id) {
    case NAT_LENGTH:
        if (t == T_NULL) return v_num(0);
        if (t == T_NUMBER) return v_num(fabs(v_dbl(in)));
        if (t == T_STRING) return v_num(str_codepoints(v_str(in)));
        if (t == T_ARRAY) return v_num(v_arr(in)->len);
        if (t == T_OBJECT) return v_num(v_obj(in)->len);
        raise2(in, " has no length", V_INVALID, "");
        return V_NULL;
    case NAT_NOT: return v_bool(!v_truthy(in));
    case NAT_TYPE: return str_cstr(type_name(in));
    case NAT_KEYS:
    case NAT_KEYS_UNSORTED:
        if (t == T_OBJECT) {
            if (id == NAT_KEYS) return obj_sorted_keys(in);
            Obj *o = v_obj(in);
            V r = arr_new(o->len);
            for (int i = 0; i < o->len; i++) v_arr(r)->items[i] = o->keys[i];
            v_arr(r)->len = o->len;
            return r;
        }
        if (t == T_ARRAY) {
            int n = v_arr(in)->len;
            V r = arr_new(n);
            for (int i = 0; i < n; i++) v_arr(r)->items[i] = v_num(i);
            v_arr(r)->len = n;
            return r;
        }
        raise2(in, " has no keys", V_INVALID, "");
        return V_NULL;
    case NAT_HAS:
        if (t == T_OBJECT && v_type(args[0]) == T_STRING) return v_bool(obj_find(v_obj(in), args[0]) >= 0);
        if (t == T_ARRAY && v_type(args[0]) == T_NUMBER) {
            double d = v_dbl(args[0]);
            return v_bool(d >= 0 && d < v_arr(in)->len);
        }
        raise2(in, ", ", args[0], ": cannot check whether one has a key");
        return V_NULL;
    case NAT_CONTAINS: {
        int ta = v_type(in), tb = v_type(args[0]);
        int bools = (ta == T_TRUE || ta == T_FALSE) && (tb == T_TRUE || tb == T_FALSE);
        if (ta != tb && !bools) {
            raise2(in, " and ", args[0], " cannot have their containment checked");
            return V_NULL;
        }
        return v_bool(contains_value(in, args[0]));
    }
    case NAT_TOSTRING: return v_tostring(in);
    case NAT_TOJSON: return v_tojson(in);
    case NAT_FROMJSON:
        if (t != T_STRING) { raise2(in, " cannot be parsed as JSON", V_INVALID, ""); return V_NULL; }
        return json_parse(v_str(in)->data, v_str(in)->len);
    case NAT_TONUMBER:
        if (t == T_NUMBER) return in;
        if (t == T_STRING) {
            Str *s = v_str(in);
            char *stop = 0;
            double d = s->len ? strtod(s->data, &stop) : 0;
            if (s->len && stop == s->data + s->len) return v_num(d);
            Buf b = {0, 0, 0};
            buf_adds(&b, "Cannot parse '");
            buf_add(&b, s->data, s->len);
            buf_adds(&b, "' as JSON");
            raise_value(buf_to_str(&b));
            return V_NULL;
        }
        raise2(in, " cannot be parsed as a number", V_INVALID, "");
        return V_NULL;
    case NAT_ASCII_UPCASE:
    case NAT_ASCII_DOWNCASE: {
        if (t != T_STRING) { raise2(in, " cannot be case-converted", V_INVALID, ""); return V_NULL; }
        Str *s = v_str(in);
        V r = str_new(s->data, s->len);
        Str *o = v_str(r);
        for (int i = 0; i < o->len; i++) {
            char c = o->data[i];
            if (id == NAT_ASCII_UPCASE && c >= 'a' && c <= 'z') o->data[i] = (char) (c - 32);
            if (id == NAT_ASCII_DOWNCASE && c >= 'A' && c <= 'Z') o->data[i] = (char) (c + 32);
        }
        return r;
    }
    case NAT_EXPLODE: {
        if (t != T_STRING) { raise2(in, " cannot be exploded", V_INVALID, ""); return V_NULL; }
        Str *s = v_str(in);
        V r = arr_new(s->len);
        for (int i = 0, cp; i < s->len;) {
            i += utf8_decode((const unsigned char *) s->data + i, s->len - i, &cp);
            arr_push(r, v_num(cp));
        }
        return r;
    }
    case NAT_IMPLODE: {
        if (t != T_ARRAY) { raise2(in, " cannot be imploded", V_INVALID, ""); return V_NULL; }
        Arr *a = v_arr(in);
        Buf b = {0, 0, 0};
        for (int i = 0; i < a->len; i++) {
            if (!v_is_num(a->items[i])) { raise_msg("Unicode codepoint must be numeric"); return V_NULL; }
            char enc[4];
            buf_add(&b, enc, utf8_encode((int) v_dbl(a->items[i]), enc));
        }
        return buf_to_str(&b);
    }
    case NAT_SPLIT: {
        if (t != T_STRING || v_type(args[0]) != T_STRING) {
            raise_msg("split input and separator must be strings");
            return V_NULL;
        }
        Str *s = v_str(in), *sep = v_str(args[0]);
        V r = arr_new(4);
        if (s->len == 0) return r;
        if (sep->len == 0) {
            for (int i = 0, cp; i < s->len;) {
                int n = utf8_decode((const unsigned char *) s->data + i, s->len - i, &cp);
                arr_push(r, str_new(s->data + i, n));
                i += n;
            }
            return r;
        }
        int start = 0;
        for (int i = 0; i + sep->len <= s->len;) {
            if (memcmp(s->data + i, sep->data, (unsigned long) sep->len) == 0) {
                arr_push(r, str_new(s->data + start, i - start));
                i += sep->len;
                start = i;
            } else {
                i++;
            }
        }
        arr_push(r, str_new(s->data + start, s->len - start));
        return r;
    }
    case NAT_JOIN: {
        if (t != T_ARRAY) { raise2(in, " cannot be joined", V_INVALID, ""); return V_NULL; }
        Arr *a = v_arr(in);
        if (a->len == 0) return str_cstr("");
        Buf b = {0, 0, 0};
        for (int i = 0; i < a->len; i++) {
            if (i && v_type(args[0]) == T_STRING) buf_add(&b, v_str(args[0])->data, v_str(args[0])->len);
            V x = a->items[i];
            if (x == V_NULL) continue;
            V text = v_type(x) == T_STRING ? x : v_tojson(x);
            buf_add(&b, v_str(text)->data, v_str(text)->len);
        }
        return buf_to_str(&b);
    }
    case NAT_ADD: {
        if (t == T_NULL) return V_NULL;
        if (t != T_ARRAY) { raise2(in, " cannot be added up", V_INVALID, ""); return V_NULL; }
        Arr *a = v_arr(in);
        if (a->len == 0) return V_NULL;
        /* accumulate into one fresh container (jq mutates its refcount-1 sum) */
        V acc = a->items[0];
        int fresh = 0;
        for (int i = 1; i < a->len; i++) {
            V x = a->items[i];
            int at = v_type(acc), xt = v_type(x);
            if (at == T_ARRAY && xt == T_ARRAY) {
                if (!fresh) { acc = arr_copy(acc, v_arr(x)->len); fresh = 1; }
                for (int j = 0; j < v_arr(x)->len; j++) arr_push(acc, v_arr(x)->items[j]);
            } else if (at == T_OBJECT && xt == T_OBJECT) {
                if (!fresh) { acc = obj_copy(acc, v_obj(x)->len); fresh = 1; }
                for (int j = 0; j < v_obj(x)->len; j++) obj_put(acc, v_obj(x)->keys[j], v_obj(x)->vals[j]);
            } else {
                acc = binop_value(OPB_ADD, acc, x);
                if (jq_err_set) return V_NULL;
                fresh = 0;
            }
        }
        return acc;
    }
    case NAT_FLATTEN: {
        if (t != T_ARRAY) { raise2(in, " cannot be flattened", V_INVALID, ""); return V_NULL; }
        V r = arr_new(v_arr(in)->len);
        flatten_into(r, in);
        return r;
    }
    case NAT_FLOOR:
    case NAT_CEIL:
        if (t != T_NUMBER) { raise2(in, " number required", V_INVALID, ""); return V_NULL; }
        return v_num(id == NAT_FLOOR ? floor(v_dbl(in)) : ceil(v_dbl(in)));
    case NAT_MIN:
    case NAT_MAX: {
        if (t != T_ARRAY) { raise2(in, " cannot be iterated", V_INVALID, ""); return V_NULL; }
        Arr *a = v_arr(in);
        if (a->len == 0) return V_NULL;
        V best = a->items[0];
        for (int i = 1; i < a->len; i++) {
            int c = v_compare(a->items[i], best);
            if (id == NAT_MIN ? c < 0 : c >= 0) best = a->items[i];
        }
        return best;
    }
    case NAT_SORT:
    case NAT_UNIQUE: {
        if (t != T_ARRAY) { raise2(in, " cannot be sorted, as it is not an array", V_INVALID, ""); return V_NULL; }
        V r = arr_copy(in, 0);
        Arr *a = v_arr(r);
        sort_values(a->items, a->len);
        if (id == NAT_UNIQUE && a->len > 1) {
            int w = 1;
            for (int i = 1; i < a->len; i++)
                if (!v_equal(a->items[i], a->items[w - 1])) a->items[w++] = a->items[i];
            a->len = w;
        }
        return r;
    }
    case NAT_REVERSE: {
        if (t == T_NULL) return arr_new(0);
        if (t == T_STRING) {
            V cps = call_native(NAT_EXPLODE, in, args);
            Arr *a = v_arr(cps);
            for (int i = 0, j = a->len - 1; i < j; i++, j--) {
                V x = a->items[i];
                a->items[i] = a->items[j];
                a->items[j] = x;
            }
            return call_native(NAT_IMPLODE, cps, args);
        }
        if (t != T_ARRAY) { raise2(in, " cannot be reversed", V_INVALID, ""); return V_NULL; }
        Arr *a = v_arr(in);
        V r = arr_new(a->len);
        for (int i = a->len - 1; i >= 0; i--) v_arr(r)->items[v_arr(r)->len++] = a->items[i];
        return r;
    }
    case NAT_GETPATH:
        if (v_type(args[0]) != T_ARRAY) { raise_msg("Path must be specified as an array"); return V_NULL; }
        return getpath_value(in, args[0]);
    case NAT_SETPATH:
        if (v_type(args[0]) != T_ARRAY) { raise_msg("Path must be specified as an array"); return V_NULL; }
        return setpath_rec(in, v_arr(args[0]), 0, args[1]);
    case NAT_DELPATHS: return delpaths_value(in, args[0]);
    case NAT_FROM_ENTRIES: {
        if (t != T_ARRAY) { raise2(in, " cannot be iterated", V_INVALID, ""); return V_NULL; }
        Arr *a = v_arr(in);
        V r = obj_new(a->len);
        static const char *key_names[] = {"key", "k", "name", "Name", "K", "Key"};
        for (int i = 0; i < a->len; i++) {
            V e = a->items[i];
            if (v_type(e) != T_OBJECT) { raise2(e, " cannot be indexed as an entry", V_INVALID, ""); return V_NULL; }
            V key = V_NULL;
            for (int k = 0; k < 6 && !v_truthy(key); k++) key = obj_get(e, str_cstr(key_names[k]));
            if (v_type(key) != T_STRING) key = v_tojson(key);
            V vkey = str_cstr("value");
            V val = obj_find(v_obj(e), vkey) >= 0 ? obj_get(e, vkey) : obj_get(e, str_cstr("v"));
            obj_put(r, key, val);
        }
        return r;
    }
    case NAT_SORT_BY_IMPL:
    case NAT_GROUP_BY_IMPL: {
        if (t != T_ARRAY || v_type(args[0]) != T_ARRAY || v_arr(args[0])->len != v_arr(in)->len) {
            raise2(in, " cannot be sorted, as it is not an array", V_INVALID, "");
            return V_NULL;
        }
        int n = v_arr(in)->len;
        int *order = sorted_order(in, args[0]);
        V *items = v_arr(in)->items, *keys = v_arr(args[0])->items;
        V r = arr_new(n);
        if (id == NAT_SORT_BY_IMPL) {
            for (int i = 0; i < n; i++) arr_push(r, items[order[i]]);
        } else {
            V group = V_NULL;
            for (int i = 0; i < n; i++) {
                if (i == 0 || !v_equal(keys[order[i]], keys[order[i - 1]])) {
                    group = arr_new(2);
                    arr_push(r, group);
                }
                arr_push(group, items[order[i]]);
            }
        }
        free(order);
        return r;
    }
    case NAT_ERROR0: raise_value(in); return V_NULL;
    case NAT_ERROR1: raise_value(args[0]); return V_NULL;
    }
    raise_msg("unknown native");
    return V_NULL;
}

/* ---------- the VM ---------- */

static int path_tracking(void) { return cur_path != V_INVALID && subexp == 0; }

static V path_append(V path, V key) {
    V c = arr_copy(path, 1);
    arr_push(c, key);
    return c;
}

/* while tracking, a path step must start from the value at the current path */
static int path_check(V t) {
    if (path_tracking() && t != cur_vat) {
        Buf b = {0, 0, 0};
        Str *d = v_str(describe(t));
        buf_adds(&b, "Invalid path expression with result ");
        buf_add(&b, d->data, d->len);
        raise_value(buf_to_str(&b));
        return 0;
    }
    return 1;
}

/* push element `i` of an iterated container (array index or object entry) */
static void each_produce(V c, int i) {
    V key, val;
    if (v_type(c) == T_ARRAY) {
        key = v_num(i);
        val = v_arr(c)->items[i];
    } else {
        key = v_obj(c)->keys[i];
        val = v_obj(c)->vals[i];
    }
    if (path_tracking()) {
        cur_path = path_append(cur_path, key);
        cur_vat = val;
    }
    st_push(val);
}

static int container_len(V c) { return v_type(c) == T_ARRAY ? v_arr(c)->len : v_obj(c)->len; }

static int range_more(double cur, double upto, double step) {
    return step > 0 ? cur < upto : (step < 0 ? cur > upto : 0);
}

/* run the compiled program on `input`; outputs collect in `outputs` */
static int vm_run(V input) {
    int *code = prog.code;
    int pc = prog.fns[0].entry;
    V empty_path = arr_new(0);
    add_const(empty_path);
    fp = frame_new(0, 0, 0, -1);
    st_push(input);
    for (;;) {
        if (gc_bytes > gc_threshold) gc_collect();
        int op = code[pc++];
        switch (op) {
        case OP_DUP: {
            V v = stv[sttop];
            st_push(v);
            break;
        }
        case OP_POP:
            st_pop();
            break;
        case OP_LOADK:
            st_pop();
            st_push(prog.consts[code[pc++]]);
            break;
        case OP_NEWARR:
            st_pop();
            st_push(arr_new(0));
            break;
        case OP_OBJ_START: {
            V in = st_pop();
            st_push(obj_new(0));
            st_push(in);
            break;
        }
        case OP_SUBEXP_BEGIN: {
            V v = stv[sttop];
            st_push(v);
            subexp++;
            break;
        }
        case OP_SUBEXP_END: {
            V a = st_pop(), b = st_pop();
            st_push(a);
            st_push(b);
            subexp--;
            break;
        }
        case OP_INDEX:
        case OP_INDEXK: {
            V t = st_pop();
            V k = op == OP_INDEXK ? prog.consts[code[pc++]] : st_pop();
            if (!path_check(t)) goto do_raise;
            V r = index_value(t, k);
            if (jq_err_set) goto do_raise;
            if (path_tracking()) {
                cur_path = path_append(cur_path, k);
                cur_vat = r;
            }
            st_push(r);
            break;
        }
        case OP_EACH: {
            V c = st_pop();
            int ct = v_type(c);
            if (ct != T_ARRAY && ct != T_OBJECT) {
                Buf b = {0, 0, 0};
                Str *d = v_str(describe(c));
                buf_adds(&b, "Cannot iterate over ");
                buf_add(&b, d->data, d->len);
                raise_value(buf_to_str(&b));
                goto do_raise;
            }
            if (!path_check(c)) goto do_raise;
            int n = container_len(c);
            if (n == 0) goto do_backtrack;
            if (n > 1) {
                Fork *f = fork_push(FK_EACH, pc);
                f->aux = c;
                f->auxi = 1;
            }
            each_produce(c, 0);
            break;
        }
        case OP_SLICE: {
            V t = st_pop(), to = st_pop(), from = st_pop();
            if (path_tracking()) {
                raise_msg("jq-core does not support slice paths");
                goto do_raise;
            }
            V r = slice_value(t, from, to);
            if (jq_err_set) goto do_raise;
            st_push(r);
            break;
        }
        case OP_FORK:
            fork_push(FK_FORK, code[pc++]);
            break;
        case OP_JUMP:
            pc = code[pc];
            break;
        case OP_JUMP_F: {
            V c = st_pop();
            if (!v_truthy(c)) pc = code[pc];
            else pc++;
            break;
        }
        case OP_JUMP_F_SUB: {
            V in = st_pop(), c = st_pop();
            st_push(in);
            if (!v_truthy(c)) pc = code[pc];
            else pc++;
            break;
        }
        case OP_JUMP_F_KEEP:
            if (!v_truthy(stv[sttop])) pc = code[pc];
            else pc++;
            break;
        case OP_BACKTRACK:
            goto do_backtrack;
        case OP_STOREV:
        case OP_STOREV_UNDER:
        case OP_LOADV:
        case OP_LOADVN:
        case OP_APPEND:
        case OP_LABEL_BEGIN:
        case OP_BREAK: {
            Frame *f = frame_hop(fp, code[pc]);
            int slot = code[pc + 1];
            pc += 2;
            if (op == OP_STOREV) {
                f->locals[slot] = st_pop();
            } else if (op == OP_STOREV_UNDER) {
                V in = st_pop();
                f->locals[slot] = st_pop();
                st_push(in);
            } else if (op == OP_LOADV || op == OP_LOADVN) {
                st_pop();
                st_push(f->locals[slot]);
                if (op == OP_LOADVN) f->locals[slot] = V_NULL;
            } else if (op == OP_APPEND) {
                /* the collector array is private to this [..] until LOADVN */
                arr_push(f->locals[slot], st_pop());
            } else if (op == OP_LABEL_BEGIN) {
                long long id = ++label_counter;
                f->locals[slot] = (TAG_LABEL << 48) | (V) id;
                Fork *k = fork_push(FK_TRY, -1);
                k->label = id;
            } else {
                raise_value(f->locals[slot]);
                goto do_raise;
            }
            break;
        }
        case OP_INSERT: {
            V in = st_pop(), val = st_pop(), key = st_pop(), obj = st_pop();
            if (v_type(key) != T_STRING) {
                Buf b = {0, 0, 0};
                Str *d = v_str(describe(key));
                buf_adds(&b, "Object keys must be strings, not ");
                buf_add(&b, d->data, d->len);
                raise_value(buf_to_str(&b));
                goto do_raise;
            }
            V c = obj_copy(obj, 1);
            obj_put(c, key, val);
            st_push(c);
            st_push(in);
            break;
        }
        case OP_RANGE: {
            int nargs = code[pc++];
            st_pop();
            V step = nargs == 3 ? st_pop() : v_num(1);
            V upto = st_pop(), from = st_pop();
            if (!v_is_num(from) || !v_is_num(upto) || !v_is_num(step)) {
                raise_msg("Range bounds must be numeric");
                goto do_raise;
            }
            double s = v_dbl(step), a = v_dbl(from), z = v_dbl(upto);
            if (!range_more(a, z, s)) goto do_backtrack;
            if (range_more(a + s, z, s)) {
                Fork *f = fork_push(FK_RANGE, pc);
                f->aux = v_num(a + s);
                f->aux2 = upto;
                f->aux3 = step;
            }
            st_push(from);
            break;
        }
        case OP_PATH_BEGIN: {
            V v = st_pop();
            st_push(cur_path);
            st_push(cur_vat);
            st_push(v_num(subexp));
            cur_path = empty_path;
            cur_vat = v;
            subexp = 0;
            st_push(v);
            break;
        }
        case OP_PATH_END: {
            V r = st_pop();
            if (!path_check(r)) goto do_raise;
            V p = cur_path;
            subexp = (int) v_dbl(st_pop());
            cur_vat = st_pop();
            cur_path = st_pop();
            st_push(p);
            break;
        }
        case OP_CALL_NATIVE: {
            int id = code[pc], nargs = code[pc + 1];
            pc += 2;
            V args[4];
            V in = st_pop();
            for (int i = nargs - 1; i >= 0; i--) args[i] = st_pop();
            if (id == NAT_GETPATH && path_tracking() && !path_check(in)) goto do_raise;
            V r = call_native(id, in, args);
            if (jq_err_set) goto do_raise;
            if (id == NAT_GETPATH && path_tracking()) {
                Arr *p = v_arr(args[0]);
                for (int i = 0; i < p->len; i++) cur_path = path_append(cur_path, p->items[i]);
                cur_vat = r;
            }
            st_push(r);
            break;
        }
        case OP_CALL_JQ:
        case OP_TAIL_CALL_JQ: {
            int fn = code[pc], hops = code[pc + 1], nargs = code[pc + 2];
            int next = pc + 3 + nargs;
            Frame *caller = fp;
            int retpc = next;
            /* a tail call reuses the frame when no fork point refers into it */
            if (op == OP_TAIL_CALL_JQ && nforks == fp->fork_base && fp->caller) {
                caller = fp->caller;
                retpc = fp->retpc;
            }
            Frame *f = frame_new(fn, frame_hop(fp, hops), caller, retpc);
            for (int i = 0; i < nargs; i++) {
                f->params[i].fn = code[pc + 3 + i];
                f->params[i].env = fp;
            }
            fp = f;
            pc = prog.fns[fn].entry;
            break;
        }
        case OP_CALL_PARAM:
        case OP_TAIL_CALL_PARAM: {
            Frame *owner = frame_hop(fp, code[pc]);
            Closure cl = owner->params[code[pc + 1]];
            int next = pc + 2;
            Frame *caller = fp;
            int retpc = next;
            if (op == OP_TAIL_CALL_PARAM && nforks == fp->fork_base && fp->caller) {
                caller = fp->caller;
                retpc = fp->retpc;
            }
            fp = frame_new(cl.fn, cl.env, caller, retpc);
            pc = prog.fns[cl.fn].entry;
            break;
        }
        case OP_RET:
            if (!fp->caller) {
                /* top level: an output of the program */
                if (noutputs == capoutputs) {
                    capoutputs = capoutputs ? capoutputs * 2 : 16;
                    outputs = (V *) realloc(outputs, sizeof(V) * (unsigned long) capoutputs);
                }
                outputs[noutputs++] = st_pop();
                goto do_backtrack;
            }
            pc = fp->retpc;
            fp = fp->caller;
            break;
        case OP_TRY_BEGIN:
            fork_push(FK_TRY, code[pc++]);
            break;
        case OP_TRY_END: {
            /* the body yielded: until backtracking re-enters it, its errors
             * are no longer this try's to catch */
            int i = nforks - 1;
            while (i >= 0 && !(forks[i].kind == FK_TRY && forks[i].active)) i--;
            if (i >= 0) {
                forks[i].active = 0;
                Fork *exit = fork_push(FK_TRY_EXIT, -1);
                exit->auxi = i;
            }
            break;
        }
        case OP_BINOP: {
            int bop = code[pc++];
            st_pop();
            V l = st_pop(), r = st_pop();
            V v = binop_value(bop, l, r);
            if (jq_err_set) goto do_raise;
            st_push(v);
            break;
        }
        case OP_NEG: {
            V v = st_pop();
            if (!v_is_num(v)) {
                raise2(v, " cannot be negated", V_INVALID, "");
                goto do_raise;
            }
            st_push(v_num(-v_dbl(v)));
            break;
        }
        case OP_TOBOOL: {
            V v = st_pop();
            st_push(v_bool(v_truthy(v)));
            break;
        }
        default:
            printf("jq-core: bad opcode %d at %d\n", op, pc - 1);
            return 0;
        }
        continue;

    do_backtrack:
        for (;;) {
            if (nforks == 0) return 1;
            Fork *f = &forks[nforks - 1];
            if (f->kind == FK_FORK) {
                fork_restore(f);
                pc = f->pc;
                nforks--;
                break;
            }
            if (f->kind == FK_EACH) {
                fork_restore(f);
                V c = f->aux;
                int i = f->auxi++;
                int resume = f->pc;
                if (f->auxi >= container_len(c)) nforks--;
                else if (sttop > stlimit) stlimit = sttop;   /* the record stays live */
                each_produce(c, i);
                pc = resume;
                break;
            }
            if (f->kind == FK_RANGE) {
                fork_restore(f);
                V cur = f->aux;
                double next = v_dbl(cur) + v_dbl(f->aux3);
                int resume = f->pc;
                if (range_more(next, v_dbl(f->aux2), v_dbl(f->aux3))) {
                    f->aux = v_num(next);
                    if (sttop > stlimit) stlimit = sttop;
                } else {
                    nforks--;
                }
                st_push(cur);
                pc = resume;
                break;
            }
            if (f->kind == FK_TRY_EXIT) forks[f->auxi].active = 1;   /* re-entering the body */
            nforks--;
        }
        continue;

    do_raise: {
        V err = jq_err_val;
        jq_err_set = 0;
        for (;;) {
            if (nforks == 0) {
                Buf b = {0, 0, 0};
                tojson_to_buf(&b, err);
                buf_addc(&b, 0);
                printf("jq: error (uncaught): %s\n", b.data);
                free(b.data);
                return 0;
            }
            Fork *f = &forks[nforks - 1];
            nforks--;
            if (f->kind != FK_TRY || !f->active) continue;
            if (f->label) {
                /* `label` catches only its own break, then backtracks */
                if (v_is_label(err) && (long long) (err & V_PTR_MASK) == f->label) {
                    fork_restore(f);
                    goto do_backtrack;
                }
                continue;
            }
            if (v_is_label(err)) continue;   /* a break passes through try */
            fork_restore(f);
            st_push(err);
            pc = f->pc;
            break;
        }
        continue;
    }
    }
}

/* ---------- benchmark harness ---------- */

enum { JQ_INPUT_NULL = 0, JQ_INPUT_JSON, JQ_INPUT_RAW };

static int jq_benchmark_main(const char *name, int input_kind, const char *input_path, long long expected) {
    char filter_path[256], error[300];
    int filter_len = 0, input_len = 0;
    snprintf(filter_path, sizeof filter_path, "test/benchmark/text/jq/%s.jq", name + 3);
    char *filter = read_file(filter_path, &filter_len);
    if (!filter) { printf("%s: cannot read %s\n", name, filter_path); return 1; }
    if (!compile_program(filter, filter_len, error, (int) sizeof error)) {
        printf("%s: %s\n", name, error);
        return 1;
    }
    V input = V_NULL;
    if (input_kind != JQ_INPUT_NULL) {
        char *text = read_file(input_path, &input_len);
        if (!text) { printf("%s: cannot read %s\n", name, input_path); return 1; }
        input = input_kind == JQ_INPUT_RAW ? str_new(text, input_len) : json_parse(text, input_len);
        if (jq_err_set) { printf("%s: bad input JSON\n", name); return 1; }
    }
    add_const(input);
    if (!vm_run(input)) return 1;
    if (noutputs != 1 || !v_is_num(outputs[0])) {
        printf("%s: expected one number, got %d outputs\n", name, noutputs);
        return 1;
    }
    long long checksum = (long long) v_dbl(outputs[0]);
    printf("%s: CHECKSUM:%lld\n", name, checksum);
    return checksum == expected ? 0 : 1;
}
