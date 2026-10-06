"""Core jq value semantics: ordering, truthiness, arithmetic, indexing, paths."""
from __future__ import annotations

import math

from .encoder import encode
from .errors import JqError


def type_name(v):
    if v is None:
        return "null"
    if v is True or v is False:
        return "boolean"
    if isinstance(v, (int, float)):
        return "number"
    if isinstance(v, str):
        return "string"
    if isinstance(v, list):
        return "array"
    if isinstance(v, dict):
        return "object"
    raise JqError("Unknown value type: %r" % (v,))


def _short(v):
    s = encode(v)
    if len(s.encode("utf-8")) < 30:
        return s
    # jq truncates long dumps in error messages: string content is cut to 24
    # bytes (on a codepoint boundary) and the dump stays well-formed.
    if isinstance(v, str):
        prefix = v.encode("utf-8")[:24].decode("utf-8", "ignore")
        return encode(prefix)[:-1] + '..."'
    return s.encode("utf-8")[:26].decode("utf-8", "ignore") + "..."


def describe(v):
    return "%s (%s)" % (type_name(v), _short(v))


def truthy(v):
    return v is not None and v is not False


_RANKS = {int: 3, float: 3, str: 4, list: 5, dict: 6, type(None): 0}


def _rank(v):
    if v is None:
        return 0
    if v is False:
        return 1
    if v is True:
        return 2
    return _RANKS[type(v)]


def jq_cmp(a, b):
    """Total order over JSON values: null < false < true < numbers < strings < arrays < objects."""
    return _cmp(a, b, 0, "Comparison too deep")


def values_equal(a, b):
    return _cmp(a, b, 0, "Equality check too deep") == 0


def _cmp(a, b, depth, too_deep_msg):
    # fast path for the overwhelmingly common scalar comparisons
    # (identity shortcuts would bypass the depth guard jq guarantees)
    ta, tb = type(a), type(b)
    if ta is tb and (ta is int or ta is str):
        return (a > b) - (a < b)
    if depth > 10000:
        raise JqError(too_deep_msg)
    ra, rb = _rank(a), _rank(b)
    if ra != rb:
        return -1 if ra < rb else 1
    if ra <= 2:  # null / false / true
        return 0
    if ra == 3:
        a_nan = isinstance(a, float) and math.isnan(a)
        b_nan = isinstance(b, float) and math.isnan(b)
        if a_nan or b_nan:
            return 0 if (a_nan and b_nan) else (-1 if a_nan else 1)
        return (a > b) - (a < b)
    if ra == 4:
        return (a > b) - (a < b)
    if ra == 5:
        for x, y in zip(a, b):
            c = _cmp(x, y, depth + 1, too_deep_msg)
            if c:
                return c
        return (len(a) > len(b)) - (len(a) < len(b))
    ka, kb = sorted(a.keys()), sorted(b.keys())
    c = _cmp(ka, kb, depth + 1, too_deep_msg)
    if c:
        return c
    for k in ka:
        c = _cmp(a[k], b[k], depth + 1, too_deep_msg)
        if c:
            return c
    return 0


def add_values(a, b):
    if a is None:
        return b
    if b is None:
        return a
    if isinstance(a, bool) or isinstance(b, bool):
        raise JqError("%s and %s cannot be added" % (describe(a), describe(b)))
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return a + b
    if isinstance(a, str) and isinstance(b, str):
        return a + b
    if isinstance(a, list) and isinstance(b, list):
        return a + b
    if isinstance(a, dict) and isinstance(b, dict):
        out = dict(a)
        out.update(b)
        return out
    raise JqError("%s and %s cannot be added" % (describe(a), describe(b)))


def sub_values(a, b):
    if (isinstance(a, (int, float)) and not isinstance(a, bool)
            and isinstance(b, (int, float)) and not isinstance(b, bool)):
        return a - b
    if isinstance(a, list) and isinstance(b, list):
        return [x for x in a if not any(values_equal(x, y) for y in b)]
    raise JqError("%s and %s cannot be subtracted" % (describe(a), describe(b)))


def _deep_merge(a, b, depth=0):
    if depth > 10000:
        raise JqError("Object merge too deep")
    out = dict(a)
    for k, v in b.items():
        if k in out and isinstance(out[k], dict) and isinstance(v, dict):
            out[k] = _deep_merge(out[k], v, depth + 1)
        else:
            out[k] = v
    return out


def mul_values(a, b):
    if isinstance(a, bool) or isinstance(b, bool):
        raise JqError("%s and %s cannot be multiplied" % (describe(a), describe(b)))
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return a * b
    if isinstance(a, str) and isinstance(b, (int, float)):
        return _repeat_string(a, b)
    if isinstance(b, str) and isinstance(a, (int, float)):
        return _repeat_string(b, a)
    if isinstance(a, dict) and isinstance(b, dict):
        return _deep_merge(a, b)
    raise JqError("%s and %s cannot be multiplied" % (describe(a), describe(b)))


def _repeat_string(s, n):
    if not n >= 0:  # negative or nan
        return None
    if isinstance(n, float) and math.isinf(n):
        raise JqError("Repeat string result too long")
    n = int(n)
    if len(s) * n > 100000000:
        raise JqError("Repeat string result too long")
    return s * n  # 0 yields "", only negative counts yield null (jq 1.8)


def _split_plain(s, sep):
    if s == "":
        return []
    if sep == "":
        return list(s)
    return s.split(sep)


def div_values(a, b):
    if (isinstance(a, (int, float)) and not isinstance(a, bool)
            and isinstance(b, (int, float)) and not isinstance(b, bool)):
        if b == 0:
            raise JqError("%s and %s cannot be divided because the divisor is zero"
                          % (describe(a), describe(b)))
        if isinstance(a, int) and isinstance(b, int) and a % b == 0:
            return a // b
        return a / b
    if isinstance(a, str) and isinstance(b, str):
        return _split_plain(a, b)
    raise JqError("%s and %s cannot be divided" % (describe(a), describe(b)))


def _c_int(x):
    """Cast like C's (long long): saturating at the int64 bounds, like jq."""
    if isinstance(x, float):
        if math.isinf(x):
            return 9223372036854775807 if x > 0 else -9223372036854775808
        return int(x)
    return x


def mod_values(a, b):
    if (isinstance(a, (int, float)) and not isinstance(a, bool)
            and isinstance(b, (int, float)) and not isinstance(b, bool)):
        if (isinstance(a, float) and math.isnan(a)) or (isinstance(b, float) and math.isnan(b)):
            return float("nan")
        ia, ib = _c_int(a), _c_int(b)
        if ib == 0:
            raise JqError("%s and %s cannot be divided (remainder) because the divisor is zero"
                          % (describe(a), describe(b)))
        r = abs(ia) % abs(ib)
        return -r if ia < 0 else r
    raise JqError("%s and %s cannot be divided" % (describe(a), describe(b)))


def neg_value(v):
    if isinstance(v, (int, float)) and not isinstance(v, bool):
        return -v
    raise JqError("%s cannot be negated" % describe(v))


def _norm_index(i, n, round_up=False):
    if isinstance(i, float):
        if math.isnan(i):
            return None
        i = int(math.ceil(i)) if round_up else int(math.floor(i))
    if i < 0:
        i += n
    return i


def index_value(container, idx):
    if isinstance(idx, dict) and "start" in idx and isinstance(container, (list, str, type(None))):
        return slice_value(container, idx.get("start"), idx.get("end"))
    if container is None:
        if isinstance(idx, (str, int, float)) and not isinstance(idx, bool):
            return None
        raise JqError("Cannot index null with %s" % type_name(idx))
    if isinstance(container, dict):
        if isinstance(idx, str):
            return container.get(idx)
        raise JqError("Cannot index object with %s" % describe(idx))
    if isinstance(container, list):
        if isinstance(idx, (int, float)) and not isinstance(idx, bool):
            i = _norm_index(idx, len(container))
            if i is not None and 0 <= i < len(container):
                return container[i]
            return None
        if isinstance(idx, list):
            return indices_impl(container, idx)
        if isinstance(idx, str):
            raise JqError('Cannot index array with "%s"' % idx)
        raise JqError("Cannot index array with %s" % type_name(idx))
    raise JqError("Cannot index %s with %s" % (type_name(container), describe(idx)))


def slice_value(container, lo, hi):
    if container is None:
        return None
    if not isinstance(container, (list, str)):
        raise JqError("Cannot index %s with object" % type_name(container))
    n = len(container)
    lo = 0 if lo is None else _norm_index(lo, n)
    hi = n if hi is None else _norm_index(hi, n, round_up=True)
    lo = 0 if lo is None else max(0, min(lo, n))
    hi = n if hi is None else max(0, min(hi, n))
    if hi < lo:
        hi = lo
    return container[lo:hi]


def iterate_value(v):
    if isinstance(v, list):
        return iter(v)
    if isinstance(v, dict):
        return iter(v.values())
    raise JqError("Cannot iterate over %s" % describe(v))


def get_path(root, path):
    if len(path) > MAX_DEPTH:
        raise JqError("Path too deep")
    cur = root
    for comp in path:
        if isinstance(comp, dict):
            cur = slice_value(cur, comp.get("start"), comp.get("end"))
        else:
            if cur is not None and not isinstance(cur, (list, dict)):
                raise JqError("Cannot index %s with %s" % (type_name(cur), describe(comp)))
            cur = index_value(cur, comp)
    return cur


def set_path(root, path, value):
    if len(path) > MAX_DEPTH:
        raise JqError("Path too deep")
    if not path:
        return value
    comp = path[0]
    rest = path[1:]
    if isinstance(comp, str):
        if root is None:
            root = {}
        if not isinstance(root, dict):
            raise JqError("Cannot index %s with %s" % (type_name(root), describe(comp)))
        out = dict(root)
        out[comp] = set_path(root.get(comp), rest, value)
        return out
    if isinstance(comp, (int, float)) and not isinstance(comp, bool):
        if root is None:
            root = []
        if not isinstance(root, list):
            raise JqError("Cannot index %s with %s" % (type_name(root), describe(comp)))
        i = _norm_index(comp, len(root))
        if i is None:
            raise JqError("Cannot set array element at NaN index")
        if i < 0:
            raise JqError("Out of bounds negative array index")
        if i > 10000000:
            raise JqError("Array index too large")
        out = list(root)
        if i >= len(out):
            out.extend([None] * (i + 1 - len(out)))
        out[i] = set_path(out[i] if i < len(root) else None, rest, value)
        return out
    if isinstance(comp, dict):
        if root is None:
            root = []
        if isinstance(root, str):
            raise JqError("Cannot update string slices")
        if not isinstance(root, list):
            raise JqError("Cannot update field at object index of array")
        n = len(root)
        lo = comp.get("start")
        hi = comp.get("end")
        lo = 0 if lo is None else _norm_index(lo, n)
        hi = n if hi is None else _norm_index(hi, n, round_up=True)
        lo = 0 if lo is None else max(0, min(lo, n))
        hi = n if hi is None else max(0, min(hi, n))
        hi = max(lo, hi)
        new = set_path(root[lo:hi], rest, value)
        if not isinstance(new, list):
            raise JqError("A slice of an array can only be assigned another array")
        return root[:lo] + new + root[hi:]
    if isinstance(comp, list):
        raise JqError("Cannot update field at array index of array")
    raise JqError("Invalid path component: %s" % type_name(comp))


def del_path(root, path):
    if len(path) > MAX_DEPTH:
        raise JqError("Path too deep")
    if root is None:
        return None
    if not path:
        return None
    comp = path[0]
    rest = path[1:]
    if isinstance(comp, str):
        if not isinstance(root, dict):
            raise JqError("Cannot delete field of %s" % type_name(root))
        if comp not in root:
            return root
        out = dict(root)
        if rest:
            out[comp] = del_path(out[comp], rest)
        else:
            del out[comp]
        return out
    if isinstance(comp, (int, float)) and not isinstance(comp, bool):
        if not isinstance(root, list):
            raise JqError("Cannot delete element of %s" % type_name(root))
        i = _norm_index(comp, len(root))
        if i is None or i < 0 or i >= len(root):
            return root
        out = list(root)
        if rest:
            out[i] = del_path(out[i], rest)
        else:
            del out[i]
        return out
    if isinstance(comp, dict):
        if not isinstance(root, list):
            raise JqError("Cannot delete slice of %s" % type_name(root))
        n = len(root)
        lo = 0 if comp.get("start") is None else max(0, min(_norm_index(comp["start"], n), n))
        hi = n if comp.get("end") is None else max(0, min(_norm_index(comp["end"], n), n))
        hi = max(lo, hi)
        if rest:
            new = del_path(root[lo:hi], rest)
            return root[:lo] + (new if isinstance(new, list) else []) + root[hi:]
        return root[:lo] + root[hi:]
    raise JqError("Invalid path component: %s" % type_name(comp))


def _resolve_path(root, path):
    """Resolve negative/float path components against the value, jq-delpaths style."""
    out = []
    cur = root
    for comp in path:
        if isinstance(cur, list):
            n = len(cur)
            if isinstance(comp, (int, float)) and not isinstance(comp, bool):
                i = _norm_index(comp, n)
                comp = i if i is not None else comp
            elif isinstance(comp, dict):
                lo = comp.get("start")
                hi = comp.get("end")
                lo = 0 if lo is None else _norm_index(lo, n)
                hi = n if hi is None else _norm_index(hi, n, round_up=True)
                lo = 0 if lo is None else max(0, min(lo, n))
                hi = n if hi is None else max(0, min(hi, n))
                comp = {"start": lo, "end": max(lo, hi)}
        out.append(comp)
        try:
            cur = get_path(cur, [comp])
        except JqError:
            cur = None
    return out


def delpaths_impl(root, paths):
    import functools
    for p in paths:
        if not isinstance(p, list):
            raise JqError("Path must be specified as an array")
    resolved = [_resolve_path(root, p) for p in paths]
    ordered = sorted(resolved, key=functools.cmp_to_key(jq_cmp), reverse=True)
    for p in ordered:
        root = del_path(root, p)
    return root


def indices_impl(a, x):
    if a is None:
        return None
    if isinstance(a, str):
        if not isinstance(x, str):
            raise JqError("Cannot search %s for %s" % (type_name(a), type_name(x)))
        if x == "":
            return None
        return [i for i in range(len(a) - len(x) + 1) if a.startswith(x, i)]
    if isinstance(a, list):
        if isinstance(x, list):
            if not x:
                return None
            m = len(x)
            return [i for i in range(len(a) - m + 1)
                    if all(values_equal(a[i + j], x[j]) for j in range(m))]
        return [i for i, e in enumerate(a) if values_equal(e, x)]
    raise JqError("Cannot search %s" % describe(a))


MAX_DEPTH = 10000


def contains_value(a, b):
    if type_name(a) != type_name(b):
        raise JqError("%s and %s cannot have their containment checked"
                      % (describe(a), describe(b)))
    return _contains(a, b, 0)


def _contains(a, b, depth):
    if depth > MAX_DEPTH:
        raise JqError("Containment check too deep")
    # plain loops, not any()/all() generator expressions: each genexpr level
    # adds C-stack frames, which caps recursion depth far below MAX_DEPTH
    if isinstance(a, dict) and isinstance(b, dict):
        for k, v in b.items():
            if k not in a or not _contains(a[k], v, depth + 1):
                return False
        return True
    if isinstance(a, list) and isinstance(b, list):
        for y in b:
            hit = False
            for x in a:
                if _contains(x, y, depth + 1):
                    hit = True
                    break
            if not hit:
                return False
        return True
    if isinstance(a, str) and isinstance(b, str):
        return b in a
    if type_name(a) == type_name(b):
        return values_equal(a, b)
    return False
