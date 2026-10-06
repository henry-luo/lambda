/* jq-core values for the C2MIR jq VM (see jq_core.h).
 *
 * A value is a 64-bit NaN-boxed word: an IEEE double, or a tagged special
 * (null/false/true/invalid, label ids) or heap pointer. Heap values are
 * immutable once published; updates copy along the path. Memory is a simple
 * mark-and-sweep heap that the VM collects only at its safe point.
 */
extern int printf(const char *, ...);
extern int snprintf(char *, unsigned long, const char *, ...);
extern void *malloc(unsigned long);
extern void *realloc(void *, unsigned long);
extern void free(void *);
extern void *memcpy(void *, const void *, unsigned long);
extern int memcmp(const void *, const void *, unsigned long);
extern unsigned long strlen(const char *);
extern double strtod(const char *, char **);
extern double floor(double);
extern double ceil(double);
extern double fabs(double);
extern void *fopen(const char *, const char *);
extern int fseek(void *, long, int);
extern long ftell(void *);
extern unsigned long fread(void *, unsigned long, unsigned long, void *);
extern int fclose(void *);

typedef unsigned long long V;

#define TAG_PTR 0xFFFCULL
#define TAG_SPEC 0xFFFDULL
#define TAG_LABEL 0xFFFEULL
#define V_NULL (TAG_SPEC << 48)
#define V_FALSE (V_NULL | 1ULL)
#define V_TRUE (V_NULL | 2ULL)
#define V_INVALID (V_NULL | 3ULL)
#define V_PTR_MASK 0x0000FFFFFFFFFFFFULL

enum { K_STRING = 1, K_ARRAY, K_OBJECT, K_FRAME };
/* jq type order: null < false < true < number < string < array < object */
enum { T_NULL = 0, T_FALSE, T_TRUE, T_NUMBER, T_STRING, T_ARRAY, T_OBJECT };

typedef struct GcHdr {
    struct GcHdr *next;
    int kind;
    int mark;
} GcHdr;

typedef struct {
    GcHdr h;
    int len;      /* bytes */
    int ascii;    /* every byte < 0x80, so bytes are code points */
    char data[1];
} Str;

typedef struct {
    GcHdr h;
    int len, cap;
    V *items;
} Arr;

typedef struct {
    GcHdr h;
    int len, cap;
    V *keys;      /* strings, insertion order */
    V *vals;
} Obj;

static GcHdr *gc_objects = 0;
static unsigned long gc_bytes = 0;
/* a test build can lower this to collect constantly */
#ifndef JQ_GC_MIN_THRESHOLD
#define JQ_GC_MIN_THRESHOLD (64UL * 1024 * 1024)
#endif
static unsigned long gc_threshold = JQ_GC_MIN_THRESHOLD;

/* error state: a raised error carries a jq value (a message or any value) */
static int jq_err_set = 0;
static V jq_err_val = 0;

static void *gc_alloc(unsigned long size, int kind) {
    GcHdr *h = (GcHdr *) malloc(size);
    h->kind = kind;
    h->mark = 0;
    h->next = gc_objects;
    gc_objects = h;
    gc_bytes += size;
    return h;
}

static V v_ptr(void *p) { return (TAG_PTR << 48) | ((V) (unsigned long) p & V_PTR_MASK); }
static void *v_heap(V v) { return (void *) (unsigned long) (v & V_PTR_MASK); }
static int v_is_ptr(V v) { return (v >> 48) == TAG_PTR; }
static int v_is_num(V v) { return (v >> 48) < TAG_PTR; }
static int v_is_label(V v) { return (v >> 48) == TAG_LABEL; }

static V v_num(double d) {
    union { double d; V u; } x;
    x.d = d;
    if (d != d) x.u = 0x7FF8000000000000ULL;   /* canonical NaN never collides with tags */
    return x.u;
}

static double v_dbl(V v) {
    union { double d; V u; } x;
    x.u = v;
    return x.d;
}

static V v_bool(int b) { return b ? V_TRUE : V_FALSE; }

static int v_type(V v) {
    if (v_is_num(v)) return T_NUMBER;
    if (v == V_NULL) return T_NULL;
    if (v == V_FALSE) return T_FALSE;
    if (v == V_TRUE) return T_TRUE;
    if (v_is_ptr(v)) {
        int k = ((GcHdr *) v_heap(v))->kind;
        if (k == K_STRING) return T_STRING;
        if (k == K_ARRAY) return T_ARRAY;
        if (k == K_OBJECT) return T_OBJECT;
    }
    return T_NULL;
}

static const char *type_name(V v) {
    switch (v_type(v)) {
    case T_NULL: return "null";
    case T_FALSE: case T_TRUE: return "boolean";
    case T_NUMBER: return "number";
    case T_STRING: return "string";
    case T_ARRAY: return "array";
    default: return "object";
    }
}

static int v_truthy(V v) { return v != V_NULL && v != V_FALSE; }

static Str *v_str(V v) { return (Str *) v_heap(v); }
static Arr *v_arr(V v) { return (Arr *) v_heap(v); }
static Obj *v_obj(V v) { return (Obj *) v_heap(v); }

/* ---------- strings ---------- */

static V str_new(const char *bytes, int len) {
    Str *s = (Str *) gc_alloc(sizeof(Str) + (unsigned long) len, K_STRING);
    int ascii = 1;
    s->len = len;
    if (len) memcpy(s->data, bytes, (unsigned long) len);
    s->data[len] = 0;
    for (int i = 0; i < len; i++) {
        if ((unsigned char) bytes[i] >= 0x80) { ascii = 0; break; }
    }
    s->ascii = ascii;
    return v_ptr(s);
}

static V str_cstr(const char *c) { return str_new(c, (int) strlen(c)); }

static int str_eq(V a, V b) {
    if (a == b) return 1;
    Str *x = v_str(a), *y = v_str(b);
    return x->len == y->len && memcmp(x->data, y->data, (unsigned long) x->len) == 0;
}

/* UTF-8 helpers: decode one code point, returning bytes consumed */
static int utf8_decode(const unsigned char *p, int n, int *cp) {
    unsigned c = p[0];
    if (c < 0x80) { *cp = (int) c; return 1; }
    if ((c >> 5) == 6 && n >= 2) { *cp = (int) (((c & 0x1F) << 6) | (p[1] & 0x3F)); return 2; }
    if ((c >> 4) == 14 && n >= 3) {
        *cp = (int) (((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F));
        return 3;
    }
    if ((c >> 3) == 30 && n >= 4) {
        *cp = (int) (((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F));
        return 4;
    }
    *cp = 0xFFFD;
    return 1;
}

static int utf8_encode(int cp, char *out) {
    if (cp < 0x80) { out[0] = (char) cp; return 1; }
    if (cp < 0x800) { out[0] = (char) (0xC0 | (cp >> 6)); out[1] = (char) (0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        out[0] = (char) (0xE0 | (cp >> 12));
        out[1] = (char) (0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char) (0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char) (0xF0 | (cp >> 18));
    out[1] = (char) (0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char) (0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char) (0x80 | (cp & 0x3F));
    return 4;
}

static int str_codepoints(Str *s) {
    if (s->ascii) return s->len;
    int count = 0, cp;
    for (int i = 0; i < s->len; count++)
        i += utf8_decode((const unsigned char *) s->data + i, s->len - i, &cp);
    return count;
}

/* byte offset of code point index `idx` (clamped to the end) */
static int str_byte_offset(Str *s, int idx) {
    if (s->ascii) return idx < s->len ? idx : s->len;
    int i = 0, cp;
    for (int n = 0; n < idx && i < s->len; n++)
        i += utf8_decode((const unsigned char *) s->data + i, s->len - i, &cp);
    return i;
}

/* growable byte buffer for building strings and JSON text */
typedef struct {
    char *data;
    int len, cap;
} Buf;

static void buf_reserve(Buf *b, int extra) {
    if (b->len + extra + 1 > b->cap) {
        int cap = b->cap ? b->cap : 64;
        while (cap < b->len + extra + 1) cap *= 2;
        b->data = (char *) realloc(b->data, (unsigned long) cap);
        b->cap = cap;
    }
}

static void buf_add(Buf *b, const char *bytes, int len) {
    buf_reserve(b, len);
    memcpy(b->data + b->len, bytes, (unsigned long) len);
    b->len += len;
}

static void buf_addc(Buf *b, char c) { buf_reserve(b, 1); b->data[b->len++] = c; }
static void buf_adds(Buf *b, const char *c) { buf_add(b, c, (int) strlen(c)); }

static V buf_to_str(Buf *b) {
    V s = str_new(b->data ? b->data : "", b->len);
    free(b->data);
    b->data = 0;
    b->len = b->cap = 0;
    return s;
}

/* ---------- arrays ---------- */

static V arr_new(int cap) {
    Arr *a = (Arr *) gc_alloc(sizeof(Arr), K_ARRAY);
    a->len = 0;
    a->cap = cap > 0 ? cap : 0;
    a->items = a->cap ? (V *) malloc(sizeof(V) * (unsigned long) a->cap) : 0;
    gc_bytes += sizeof(V) * (unsigned long) a->cap;
    return v_ptr(a);
}

/* in-place append: only for arrays the caller still owns exclusively */
static void arr_push(V av, V item) {
    Arr *a = v_arr(av);
    if (a->len == a->cap) {
        int cap = a->cap ? a->cap * 2 : 4;
        a->items = (V *) realloc(a->items, sizeof(V) * (unsigned long) cap);
        gc_bytes += sizeof(V) * (unsigned long) (cap - a->cap);
        a->cap = cap;
    }
    a->items[a->len++] = item;
}

static V arr_copy(V av, int extra) {
    Arr *a = v_arr(av);
    V c = arr_new(a->len + extra);
    Arr *b = v_arr(c);
    if (a->len) memcpy(b->items, a->items, sizeof(V) * (unsigned long) a->len);
    b->len = a->len;
    return c;
}

/* ---------- objects ---------- */

static V obj_new(int cap) {
    Obj *o = (Obj *) gc_alloc(sizeof(Obj), K_OBJECT);
    o->len = 0;
    o->cap = cap > 0 ? cap : 0;
    o->keys = o->cap ? (V *) malloc(sizeof(V) * (unsigned long) o->cap) : 0;
    o->vals = o->cap ? (V *) malloc(sizeof(V) * (unsigned long) o->cap) : 0;
    gc_bytes += 2 * sizeof(V) * (unsigned long) o->cap;
    return v_ptr(o);
}

static int obj_find(Obj *o, V key) {
    Str *k = v_str(key);
    for (int i = 0; i < o->len; i++) {
        V other = o->keys[i];
        if (other == key) return i;
        Str *s = v_str(other);
        if (s->len == k->len && memcmp(s->data, k->data, (unsigned long) k->len) == 0) return i;
    }
    return -1;
}

static V obj_get(V ov, V key) {
    Obj *o = v_obj(ov);
    int i = obj_find(o, key);
    return i < 0 ? V_NULL : o->vals[i];
}

/* in-place set: only for objects the caller still owns exclusively */
static void obj_put(V ov, V key, V val) {
    Obj *o = v_obj(ov);
    int i = obj_find(o, key);
    if (i >= 0) { o->vals[i] = val; return; }
    if (o->len == o->cap) {
        int cap = o->cap ? o->cap * 2 : 4;
        o->keys = (V *) realloc(o->keys, sizeof(V) * (unsigned long) cap);
        o->vals = (V *) realloc(o->vals, sizeof(V) * (unsigned long) cap);
        gc_bytes += 2 * sizeof(V) * (unsigned long) (cap - o->cap);
        o->cap = cap;
    }
    o->keys[o->len] = key;
    o->vals[o->len] = val;
    o->len++;
}

static V obj_copy(V ov, int extra) {
    Obj *o = v_obj(ov);
    V c = obj_new(o->len + extra);
    Obj *p = v_obj(c);
    if (o->len) {
        memcpy(p->keys, o->keys, sizeof(V) * (unsigned long) o->len);
        memcpy(p->vals, o->vals, sizeof(V) * (unsigned long) o->len);
    }
    p->len = o->len;
    return c;
}

/* copy without key i */
static V obj_without(V ov, int skip) {
    Obj *o = v_obj(ov);
    V c = obj_new(o->len);
    Obj *p = v_obj(c);
    for (int i = 0; i < o->len; i++) {
        if (i == skip) continue;
        p->keys[p->len] = o->keys[i];
        p->vals[p->len] = o->vals[i];
        p->len++;
    }
    return c;
}

/* ---------- errors ---------- */

static void raise_value(V v) {
    if (!jq_err_set) {
        jq_err_set = 1;
        jq_err_val = v;
    }
}

static void raise_msg(const char *msg) { raise_value(str_cstr(msg)); }

/* ---------- comparison (jq total order) ---------- */

static int v_compare(V a, V b);

static int str_compare(V a, V b) {
    Str *x = v_str(a), *y = v_str(b);
    int n = x->len < y->len ? x->len : y->len;
    int c = memcmp(x->data, y->data, (unsigned long) n);
    if (c) return c < 0 ? -1 : 1;
    return x->len < y->len ? -1 : (x->len > y->len ? 1 : 0);
}

static void sort_values(V *items, int n);
static V obj_sorted_keys(V ov);

static int v_compare(V a, V b) {
    int ta = v_type(a), tb = v_type(b);
    if (ta != tb) return ta < tb ? -1 : 1;
    switch (ta) {
    case T_NUMBER: {
        double x = v_dbl(a), y = v_dbl(b);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    case T_STRING:
        return a == b ? 0 : str_compare(a, b);
    case T_ARRAY: {
        Arr *x = v_arr(a), *y = v_arr(b);
        for (int i = 0; i < x->len && i < y->len; i++) {
            int c = v_compare(x->items[i], y->items[i]);
            if (c) return c;
        }
        return x->len < y->len ? -1 : (x->len > y->len ? 1 : 0);
    }
    case T_OBJECT: {
        if (a == b) return 0;
        V ka = obj_sorted_keys(a), kb = obj_sorted_keys(b);
        int c = v_compare(ka, kb);
        if (c) return c;
        Arr *k = v_arr(ka);
        for (int i = 0; i < k->len; i++) {
            c = v_compare(obj_get(a, k->items[i]), obj_get(b, k->items[i]));
            if (c) return c;
        }
        return 0;
    }
    default:
        return 0;
    }
}

static int v_equal(V a, V b) { return a == b || v_compare(a, b) == 0; }

/* stable merge sort of values by v_compare */
static void merge_sort_values(V *items, V *tmp, int n) {
    if (n < 2) return;
    int h = n / 2;
    merge_sort_values(items, tmp, h);
    merge_sort_values(items + h, tmp, n - h);
    int i = 0, j = h, k = 0;
    while (i < h && j < n) tmp[k++] = v_compare(items[j], items[i]) < 0 ? items[j++] : items[i++];
    while (i < h) tmp[k++] = items[i++];
    while (j < n) tmp[k++] = items[j++];
    memcpy(items, tmp, sizeof(V) * (unsigned long) n);
}

static void sort_values(V *items, int n) {
    if (n < 2) return;
    V *tmp = (V *) malloc(sizeof(V) * (unsigned long) n);
    merge_sort_values(items, tmp, n);
    free(tmp);
}

static V obj_sorted_keys(V ov) {
    Obj *o = v_obj(ov);
    V r = arr_new(o->len);
    Arr *a = v_arr(r);
    for (int i = 0; i < o->len; i++) a->items[i] = o->keys[i];
    a->len = o->len;
    sort_values(a->items, a->len);
    return r;
}

/* ---------- JSON text ---------- */

static void num_to_buf(Buf *b, double d) {
    char tmp[64];
    if (d != d) { buf_adds(b, "null"); return; }
    if (d == floor(d) && fabs(d) < 1e17) {
        snprintf(tmp, sizeof tmp, "%lld", (long long) d);
    } else if (d > 1.7976931348623157e308) {
        snprintf(tmp, sizeof tmp, "1.7976931348623157e+308");
    } else if (d < -1.7976931348623157e308) {
        snprintf(tmp, sizeof tmp, "-1.7976931348623157e+308");
    } else {
        snprintf(tmp, sizeof tmp, "%.17g", d);
    }
    buf_adds(b, tmp);
}

static void json_string_to_buf(Buf *b, V s) {
    static const char hex[] = "0123456789abcdef";
    Str *x = v_str(s);
    buf_addc(b, '"');
    for (int i = 0; i < x->len; i++) {
        unsigned char c = (unsigned char) x->data[i];
        switch (c) {
        case '"': buf_adds(b, "\\\""); break;
        case '\\': buf_adds(b, "\\\\"); break;
        case '\n': buf_adds(b, "\\n"); break;
        case '\t': buf_adds(b, "\\t"); break;
        case '\r': buf_adds(b, "\\r"); break;
        case '\b': buf_adds(b, "\\b"); break;
        case '\f': buf_adds(b, "\\f"); break;
        default:
            if (c < 0x20 || c == 0x7F) {
                char esc[7] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15], 0};
                buf_adds(b, esc);
            } else {
                buf_addc(b, (char) c);
            }
        }
    }
    buf_addc(b, '"');
}

static void tojson_to_buf(Buf *b, V v) {
    switch (v_type(v)) {
    case T_NULL: buf_adds(b, "null"); break;
    case T_FALSE: buf_adds(b, "false"); break;
    case T_TRUE: buf_adds(b, "true"); break;
    case T_NUMBER: num_to_buf(b, v_dbl(v)); break;
    case T_STRING: json_string_to_buf(b, v); break;
    case T_ARRAY: {
        Arr *a = v_arr(v);
        buf_addc(b, '[');
        for (int i = 0; i < a->len; i++) {
            if (i) buf_addc(b, ',');
            tojson_to_buf(b, a->items[i]);
        }
        buf_addc(b, ']');
        break;
    }
    default: {
        Obj *o = v_obj(v);
        buf_addc(b, '{');
        for (int i = 0; i < o->len; i++) {
            if (i) buf_addc(b, ',');
            json_string_to_buf(b, o->keys[i]);
            buf_addc(b, ':');
            tojson_to_buf(b, o->vals[i]);
        }
        buf_addc(b, '}');
    }
    }
}

static V v_tojson(V v) {
    Buf b = {0, 0, 0};
    tojson_to_buf(&b, v);
    return buf_to_str(&b);
}

static V v_tostring(V v) { return v_type(v) == T_STRING ? v : v_tojson(v); }

/* JSON parser (fixture loading and fromjson) */
typedef struct {
    const char *p, *end;
    int failed;
} JsonIn;

static void json_ws(JsonIn *in) {
    while (in->p < in->end && (*in->p == ' ' || *in->p == '\n' || *in->p == '\t' || *in->p == '\r')) in->p++;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static V json_value(JsonIn *in);

static V json_string(JsonIn *in) {
    Buf b = {0, 0, 0};
    in->p++;   /* opening quote */
    while (in->p < in->end && *in->p != '"') {
        char c = *in->p++;
        if (c != '\\') { buf_addc(&b, c); continue; }
        if (in->p >= in->end) break;
        c = *in->p++;
        switch (c) {
        case 'n': buf_addc(&b, '\n'); break;
        case 't': buf_addc(&b, '\t'); break;
        case 'r': buf_addc(&b, '\r'); break;
        case 'b': buf_addc(&b, '\b'); break;
        case 'f': buf_addc(&b, '\f'); break;
        case 'u': {
            int cp = 0;
            for (int i = 0; i < 4 && in->p < in->end; i++) cp = cp * 16 + hex_digit(*in->p++);
            if (cp >= 0xD800 && cp < 0xDC00 && in->p + 6 <= in->end && in->p[0] == '\\' && in->p[1] == 'u') {
                int lo = 0;
                in->p += 2;
                for (int i = 0; i < 4; i++) lo = lo * 16 + hex_digit(*in->p++);
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            }
            char enc[4];
            buf_add(&b, enc, utf8_encode(cp, enc));
            break;
        }
        default: buf_addc(&b, c);
        }
    }
    if (in->p >= in->end) in->failed = 1;
    else in->p++;
    return buf_to_str(&b);
}

static V json_value(JsonIn *in) {
    json_ws(in);
    if (in->p >= in->end) { in->failed = 1; return V_NULL; }
    char c = *in->p;
    if (c == '{') {
        V o = obj_new(4);
        in->p++;
        json_ws(in);
        if (in->p < in->end && *in->p == '}') { in->p++; return o; }
        for (;;) {
            json_ws(in);
            if (in->p >= in->end || *in->p != '"') { in->failed = 1; return o; }
            V k = json_string(in);
            json_ws(in);
            if (in->p >= in->end || *in->p != ':') { in->failed = 1; return o; }
            in->p++;
            V val = json_value(in);
            if (in->failed) return o;
            obj_put(o, k, val);
            json_ws(in);
            if (in->p < in->end && *in->p == ',') { in->p++; continue; }
            if (in->p < in->end && *in->p == '}') { in->p++; return o; }
            in->failed = 1;
            return o;
        }
    }
    if (c == '[') {
        V a = arr_new(4);
        in->p++;
        json_ws(in);
        if (in->p < in->end && *in->p == ']') { in->p++; return a; }
        for (;;) {
            V item = json_value(in);
            if (in->failed) return a;
            arr_push(a, item);
            json_ws(in);
            if (in->p < in->end && *in->p == ',') { in->p++; continue; }
            if (in->p < in->end && *in->p == ']') { in->p++; return a; }
            in->failed = 1;
            return a;
        }
    }
    if (c == '"') return json_string(in);
    if (in->end - in->p >= 4 && memcmp(in->p, "true", 4) == 0) { in->p += 4; return V_TRUE; }
    if (in->end - in->p >= 5 && memcmp(in->p, "false", 5) == 0) { in->p += 5; return V_FALSE; }
    if (in->end - in->p >= 4 && memcmp(in->p, "null", 4) == 0) { in->p += 4; return V_NULL; }
    if (c == '-' || (c >= '0' && c <= '9')) {
        char tmp[64];
        int n = 0;
        while (in->p < in->end && n < 63) {
            char d = *in->p;
            if ((d >= '0' && d <= '9') || d == '-' || d == '+' || d == '.' || d == 'e' || d == 'E') {
                tmp[n++] = d;
                in->p++;
            } else {
                break;
            }
        }
        tmp[n] = 0;
        char *stop = 0;
        double d = strtod(tmp, &stop);
        if (stop != tmp + n) in->failed = 1;
        return v_num(d);
    }
    in->failed = 1;
    return V_NULL;
}

/* parse a whole JSON text; sets jq_err on failure */
static V json_parse(const char *text, int len) {
    JsonIn in;
    in.p = text;
    in.end = text + len;
    in.failed = 0;
    V v = json_value(&in);
    json_ws(&in);
    if (in.failed || in.p != in.end) {
        Buf b = {0, 0, 0};
        buf_adds(&b, "Invalid JSON text: ");
        buf_add(&b, text, len > 60 ? 60 : len);
        raise_value(buf_to_str(&b));
        return V_NULL;
    }
    return v;
}

static char *read_file(const char *path, int *out_len) {
    void *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, 2);
    long n = ftell(f);
    fseek(f, 0, 0);
    char *data = (char *) malloc((unsigned long) n + 1);
    unsigned long got = fread(data, 1, (unsigned long) n, f);
    fclose(f);
    data[got] = 0;
    *out_len = (int) got;
    return data;
}
