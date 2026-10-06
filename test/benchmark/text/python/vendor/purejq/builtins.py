"""Python-native jq builtins. Derived builtins live in prelude.py as jq source."""
from __future__ import annotations

import base64
import calendar
import functools
import math
import os
import re as _re
import sys
import time as _time
import urllib.parse

from . import ops
from .encoder import encode, format_number
from .errors import Halt, JqError
from .ops import describe, jq_cmp, truthy, type_name

PY_BUILTINS = {}
PY_BUILTINS_PATH = {}


def _register(name, arity):
    def deco(fn):
        PY_BUILTINS[(name, arity)] = fn
        return fn
    return deco


def _single(arg, input):
    """Evaluate a filter argument expecting exactly one value (uses the first)."""
    for v in arg.vals(input):
        return v
    raise JqError("Filter argument produced no values")


# --- streams / errors --------------------------------------------------------

@_register("empty", 0)
def _empty(input, env):
    return iter(())


@_register("not", 0)
def _not(input, env):
    yield input is None or input is False


@_register("select", 1)
def _select(input, env, f):
    g = f.g
    if g is not None:
        v = g(input, f.env)
        if v is not None and v is not False:
            yield input
        return
    for v in f.vals(input):
        if v is not None and v is not False:
            yield input


def _select_path(input, path, env, f):
    for v in f.vals(input):
        if v is not None and v is not False:
            yield path, input


PY_BUILTINS_PATH[("select", 1)] = _select_path


@_register("map", 1)
def _map(input, env, f):
    g = f.g
    if g is not None:
        fenv = f.env
        yield [g(x, fenv) for x in ops.iterate_value(input)]
        return
    out = []
    for x in ops.iterate_value(input):
        out.extend(f.vals(x))
    yield out


@_register("error", 0)
def _error(input, env):
    raise JqError(input)
    yield  # pragma: no cover


@_register("halt", 0)
def _halt(input, env):
    raise Halt(0)
    yield  # pragma: no cover


@_register("halt_error", 0)
def _halt_error0(input, env):
    raise Halt(5, input)
    yield  # pragma: no cover


@_register("halt_error", 1)
def _halt_error1(input, env, code):
    c = _single(code, input)
    if not isinstance(c, int):
        raise JqError("halt_error/1: number required")
    raise Halt(c, input)
    yield  # pragma: no cover


@_register("debug", 0)
def _debug(input, env):
    sys.stderr.write(encode(["DEBUG:", input]) + "\n")
    yield input


@_register("stderr", 0)
def _stderr(input, env):
    sys.stderr.write(encode(input))
    yield input


@_register("input", 0)
def _input(input, env):
    it = env.get_inputs()
    if it is None:
        raise JqError("break")
    try:
        yield next(it)
    except StopIteration:
        raise JqError("break")


@_register("inputs", 0)
def _inputs(input, env):
    it = env.get_inputs()
    if it is None:
        return
    for v in it:
        yield v


@_register("input_line_number", 0)
def _iln(input, env):
    yield 0


@_register("input_filename", 0)
def _ifn(input, env):
    yield None


@_register("$ENV", 0)
def _dollar_env(input, env):
    yield dict(os.environ)


@_register("env", 0)
def _env(input, env):
    yield dict(os.environ)


@_register("builtins", 0)
def _builtins(input, env):
    from .prelude import prelude_names
    names = set("%s/%d" % k for k in PY_BUILTINS)
    names.update(prelude_names())
    yield sorted(n for n in names if not n.startswith(("_", "$")))


# --- basic inspection --------------------------------------------------------

@_register("type", 0)
def _type(input, env):
    yield type_name(input)


@_register("length", 0)
def _length(input, env):
    if input is None:
        yield 0
    elif isinstance(input, bool):
        raise JqError("boolean (%s) has no length" % encode(input))
    elif isinstance(input, (int, float)):
        yield abs(input)
    elif isinstance(input, (str, list, dict)):
        yield len(input)


@_register("utf8bytelength", 0)
def _utf8bytelength(input, env):
    if not isinstance(input, str):
        raise JqError("%s only strings have UTF-8 byte length" % describe(input))
    yield len(input.encode("utf-8"))


@_register("keys", 0)
def _keys(input, env):
    if isinstance(input, dict):
        yield sorted(input.keys())
    elif isinstance(input, list):
        yield list(range(len(input)))
    else:
        raise JqError("%s has no keys" % describe(input))


@_register("keys_unsorted", 0)
def _keys_unsorted(input, env):
    if isinstance(input, dict):
        yield list(input.keys())
    elif isinstance(input, list):
        yield list(range(len(input)))
    else:
        raise JqError("%s has no keys" % describe(input))


@_register("has", 1)
def _has(input, env, key):
    for k in key.vals(input):
        if isinstance(input, dict):
            if not isinstance(k, str):
                raise JqError("Cannot check whether object has a key of type %s" % type_name(k))
            yield k in input
        elif isinstance(input, list):
            if isinstance(k, bool) or not isinstance(k, (int, float)):
                raise JqError("Cannot check whether array has a key of type %s" % type_name(k))
            if isinstance(k, float) and (math.isnan(k) or math.isinf(k)):
                yield False
            else:
                yield 0 <= int(k) < len(input)
        else:
            raise JqError("Cannot check whether %s has a key" % type_name(input))


@_register("contains", 1)
def _contains(input, env, other):
    for o in other.vals(input):
        yield ops.contains_value(input, o)


@_register("indices", 1)
def _indices(input, env, needle):
    for x in needle.vals(input):
        yield ops.indices_impl(input, x)


# --- paths ---------------------------------------------------------------------

@_register("path", 1)
def _path(input, env, f):
    for p, _v in f.paths(input, []):
        yield p


@_register("getpath", 1)
def _getpath(input, env, parg):
    for p in parg.vals(input):
        if not isinstance(p, list):
            raise JqError("Path must be specified as an array")
        try:
            yield ops.get_path(input, p)
        except JqError:
            raise


def _getpath_path(input, path, env, parg):
    for p in parg.vals(input):
        if not isinstance(p, list):
            raise JqError("Path must be specified as an array")
        yield path + list(p), ops.get_path(input, p)


PY_BUILTINS_PATH[("getpath", 1)] = _getpath_path


def _empty_path(input, path, env):
    return iter(())


PY_BUILTINS_PATH[("empty", 0)] = _empty_path


def _error_path(input, path, env):
    raise JqError(input)
    yield  # pragma: no cover


PY_BUILTINS_PATH[("error", 0)] = _error_path


@_register("setpath", 2)
def _setpath(input, env, parg, varg):
    for p in parg.vals(input):
        if not isinstance(p, list):
            raise JqError("Path must be specified as an array")
        for v in varg.vals(input):
            yield ops.set_path(input, p, v)


@_register("delpaths", 1)
def _delpaths(input, env, parg):
    for plist in parg.vals(input):
        if not isinstance(plist, list):
            raise JqError("Paths must be specified as an array")
        yield ops.delpaths_impl(input, plist)


# --- conversions -----------------------------------------------------------------

@_register("tostring", 0)
def _tostring(input, env):
    yield input if isinstance(input, str) else encode(input)


@_register("tonumber", 0)
def _tonumber(input, env):
    if isinstance(input, bool):
        raise JqError("Cannot parse '%s' as number" % input)
    if isinstance(input, (int, float)):
        yield input
        return
    if isinstance(input, str):
        # jq accepts "+5.43" and ".89" but not surrounding whitespace
        if not _re.match(r"^[+-]?(\d+(\.\d*)?|\.\d+)([eE][+-]?\d+)?$", input):
            raise JqError("%s cannot be parsed as a number" % describe(input))
        if _re.match(r"^[+-]?\d+$", input):
            yield int(input)
        else:
            yield float(input)
        return
    raise JqError("%s cannot be parsed as a number" % describe(input))


@_register("tojson", 0)
def _tojson(input, env):
    yield encode(input)


@_register("fromjson", 0)
def _fromjson(input, env):
    import json
    if not isinstance(input, str):
        raise JqError("%s cannot be parsed as JSON" % describe(input))
    s = input.strip()
    if s in ("nan", "-nan", "NaN", "-NaN"):
        yield float("nan")
        return
    if len(input) > 2000 and _nesting_depth(input) > 1000:
        # json.loads parses recursively on the C stack, which aborts the
        # interpreter on some Python versions long before jq's depth limit;
        # deep documents go through an explicit-stack parser instead
        yield _parse_deep_json(input)
        return
    try:
        yield json.loads(input)
    except RecursionError:
        raise JqError("Exceeds depth limit for parsing")
    except ValueError as e:
        raise JqError("%s cannot be parsed as JSON: %s" % (describe(input), e))


def _nesting_depth(s):
    depth = deepest = 0
    in_str = esc = False
    for ch in s:
        if in_str:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                in_str = False
        elif ch == '"':
            in_str = True
        elif ch == "[" or ch == "{":
            depth += 1
            if depth > deepest:
                deepest = depth
        elif ch == "]" or ch == "}":
            depth -= 1
    return deepest


_NUMBER_RE = _re.compile(r"-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][-+]?\d+)?")
_MAX_PARSING_DEPTH = 10000  # matches jq's src/jv_parse.c


def _parse_deep_json(s):
    """Iterative JSON parser (explicit stack), used for deeply nested input."""
    from json.decoder import scanstring

    def bad(i, why="Invalid JSON"):
        return JqError("%s at character %d (while parsing deep JSON)" % (why, i))

    i, n = 0, len(s)
    stack = []  # (container, pending_object_key)
    result = []

    def attach(v, i):
        if not stack:
            result.append(v)
            return
        container, key = stack[-1]
        if isinstance(container, list):
            container.append(v)
        else:
            stack[-1] = (container, None)
            container[key] = v

    expect_value = True
    while True:
        while i < n and s[i] in " \t\n\r":
            i += 1
        if i >= n:
            if not expect_value and not stack and result:
                return result[0]
            raise bad(i, "Unexpected end of input")
        if not expect_value and not stack and result:
            raise bad(i, "Trailing data")
        ch = s[i]
        if expect_value:
            if ch == "[" or ch == "{":
                if len(stack) >= _MAX_PARSING_DEPTH:
                    raise JqError("Exceeds depth limit for parsing")
                container = [] if ch == "[" else {}
                attach(container, i)
                stack.append((container, None))
                i += 1
                # tolerate immediately-closed containers
                while i < n and s[i] in " \t\n\r":
                    i += 1
                if i < n and ((ch == "[" and s[i] == "]") or (ch == "{" and s[i] == "}")):
                    stack.pop()
                    i += 1
                    expect_value = False
                elif ch == "{":
                    expect_value = False  # expect a key next
                    continue
                continue
            if ch == '"':
                v, i = scanstring(s, i + 1)
                attach(v, i)
                expect_value = False
                continue
            m = _NUMBER_RE.match(s, i)
            if m:
                text = m.group()
                attach(int(text) if _re.match(r"^-?\d+$", text) else float(text), i)
                i = m.end()
                expect_value = False
                continue
            for lit, v in (("true", True), ("false", False), ("null", None)):
                if s.startswith(lit, i):
                    attach(v, i)
                    i += len(lit)
                    expect_value = False
                    break
            else:
                raise bad(i)
            continue
        # after a value (or awaiting an object key)
        if stack and isinstance(stack[-1][0], dict) and stack[-1][1] is None and ch == '"':
            key, i = scanstring(s, i + 1)
            while i < n and s[i] in " \t\n\r":
                i += 1
            if i >= n or s[i] != ":":
                raise bad(i, "Expected ':'")
            stack[-1] = (stack[-1][0], key)
            i += 1
            expect_value = True
            continue
        if ch == ",":
            if not stack:
                raise bad(i)
            i += 1
            if isinstance(stack[-1][0], dict):
                while i < n and s[i] in " \t\n\r":
                    i += 1
                if i >= n or s[i] != '"':
                    raise bad(i, "Expected object key")
                expect_value = False
            else:
                expect_value = True
            continue
        if ch == "]" or ch == "}":
            if not stack:
                raise bad(i)
            container, key = stack.pop()
            wanted = "]" if isinstance(container, list) else "}"
            if ch != wanted or key is not None:
                raise bad(i)
            i += 1
            if not stack:
                while i < n and s[i] in " \t\n\r":
                    i += 1
                if i < n:
                    raise bad(i, "Trailing data")
                return result[0]
            continue
        raise bad(i)


@_register("explode", 0)
def _explode(input, env):
    if not isinstance(input, str):
        raise JqError("%s cannot be exploded" % describe(input))
    yield [ord(c) for c in input]


@_register("implode", 0)
def _implode(input, env):
    if not isinstance(input, list):
        raise JqError("implode input must be an array")
    out = []
    for c in input:
        bad = (isinstance(c, bool) or not isinstance(c, (int, float))
               or (isinstance(c, float) and (math.isnan(c) or math.isinf(c))))
        if bad:
            raise JqError("%s can't be imploded, unicode codepoint needs to be numeric"
                          % describe(c))
        cp = int(c)
        if cp < 0 or cp > 0x10FFFF or 0xD800 <= cp <= 0xDFFF:
            cp = 0xFFFD  # replacement character, like jq
        out.append(chr(cp))
    yield "".join(out)


_LOWER = str.maketrans({chr(c): chr(c + 32) for c in range(ord("A"), ord("Z") + 1)})
_UPPER = str.maketrans({chr(c): chr(c - 32) for c in range(ord("a"), ord("z") + 1)})


@_register("ascii_downcase", 0)
def _ascii_downcase(input, env):
    if not isinstance(input, str):
        raise JqError("%s cannot be downcased" % describe(input))
    yield input.translate(_LOWER)


@_register("ascii_upcase", 0)
def _ascii_upcase(input, env):
    if not isinstance(input, str):
        raise JqError("%s cannot be upcased" % describe(input))
    yield input.translate(_UPPER)


# --- string helpers -----------------------------------------------------------------

@_register("ltrimstr", 1)
def _ltrimstr(input, env, arg):
    for s in arg.vals(input):
        if not isinstance(input, str) or not isinstance(s, str):
            raise JqError("startswith() requires string inputs")
        yield input[len(s):] if input.startswith(s) else input


@_register("rtrimstr", 1)
def _rtrimstr(input, env, arg):
    for s in arg.vals(input):
        if not isinstance(input, str) or not isinstance(s, str):
            raise JqError("endswith() requires string inputs")
        yield input[:-len(s)] if s and input.endswith(s) else input


@_register("startswith", 1)
def _startswith(input, env, arg):
    for s in arg.vals(input):
        if not isinstance(input, str) or not isinstance(s, str):
            raise JqError("startswith() requires string inputs")
        yield input.startswith(s)


@_register("endswith", 1)
def _endswith(input, env, arg):
    for s in arg.vals(input):
        if not isinstance(input, str) or not isinstance(s, str):
            raise JqError("endswith() requires string inputs")
        yield input.endswith(s)


@_register("split", 1)
def _split1(input, env, sep):
    for s in sep.vals(input):
        if not isinstance(input, str) or not isinstance(s, str):
            raise JqError("split input and separator must be strings")
        yield ops.div_values(input, s)


@_register("join", 1)
def _join(input, env, sep):
    for s in sep.vals(input):
        if not isinstance(input, list):
            raise JqError("Cannot iterate over %s" % describe(input))
        if not isinstance(s, str):
            raise JqError("join separator must be a string")
        acc = None
        for item in input:
            if item is None:
                item = ""
            elif isinstance(item, bool):
                item = "true" if item else "false"
            elif isinstance(item, (int, float)):
                item = format_number(item)
            if acc is None:
                acc = ops.add_values("", item)
            else:
                acc = ops.add_values(ops.add_values(acc, s), item)
        yield acc if acc is not None else ""


# --- regex (Python `re`; close to but not identical with Oniguruma) -------------------

_NAMED_GROUP_RE = _re.compile(r"\(\?<([A-Za-z_][A-Za-z0-9_]*)>")


def _compile_regex(pattern, flags):
    if not isinstance(pattern, str):
        raise JqError("%s cannot be matched, as it is not a string" % describe(pattern))
    pyflags = 0
    glob = False
    if flags:
        if not isinstance(flags, str):
            raise JqError("%s is not a string" % describe(flags))
        for f in flags:
            if f == "g":
                glob = True
            elif f == "i":
                pyflags |= _re.IGNORECASE
            elif f == "x":
                pyflags |= _re.VERBOSE
            elif f == "s":
                pyflags |= _re.DOTALL
            elif f == "m":
                pyflags |= _re.MULTILINE
            elif f in ("n", "l", "p"):
                pass
            else:
                raise JqError("%s is not a valid modifier string" % flags)
    converted = _NAMED_GROUP_RE.sub(r"(?P<\1>", pattern)
    try:
        return _re.compile(converted, pyflags), glob
    except _re.error as e:
        raise JqError("%s (while regex-compiling %s)" % (e, pattern))


def _match_obj(m):
    names = {v: k for k, v in m.re.groupindex.items()}
    captures = []
    for i in range(1, m.re.groups + 1):
        g = m.group(i)
        captures.append({
            "offset": m.start(i) if g is not None else -1,
            "length": (m.end(i) - m.start(i)) if g is not None else 0,
            "string": g,
            "name": names.get(i),
        })
    return {
        "offset": m.start(),
        "length": m.end() - m.start(),
        "string": m.group(),
        "captures": captures,
    }


def _iter_matches(rx, s, glob):
    if not glob:
        m = rx.search(s)
        if m is not None:
            yield m
        return
    pos = 0
    while pos <= len(s):
        m = rx.search(s, pos)
        if m is None:
            return
        yield m
        pos = m.end() + 1 if m.end() == m.start() else m.end()


def _regex_args(input, env, reArg, flagsArg):
    if not isinstance(input, str):
        raise JqError("%s cannot be matched, as it is not a string" % describe(input))
    for rv in reArg.vals(input):
        flags_values = flagsArg.vals(input) if flagsArg is not None else (None,)
        for fv in flags_values:
            yield _compile_regex(rv, fv)


@_register("test", 1)
def _test1(input, env, reArg):
    for rx, glob in _regex_args(input, env, reArg, None):
        yield rx.search(input) is not None


@_register("test", 2)
def _test2(input, env, reArg, flagsArg):
    for rx, glob in _regex_args(input, env, reArg, flagsArg):
        yield rx.search(input) is not None


@_register("match", 1)
def _match1(input, env, reArg):
    for rx, glob in _regex_args(input, env, reArg, None):
        for m in _iter_matches(rx, input, glob):
            yield _match_obj(m)


@_register("match", 2)
def _match2(input, env, reArg, flagsArg):
    for rx, glob in _regex_args(input, env, reArg, flagsArg):
        for m in _iter_matches(rx, input, glob):
            yield _match_obj(m)


def _capture_obj(m):
    return {k: m.group(k) for k in m.re.groupindex}


@_register("capture", 1)
def _capture1(input, env, reArg):
    for rx, glob in _regex_args(input, env, reArg, None):
        for m in _iter_matches(rx, input, glob):
            yield _capture_obj(m)


@_register("capture", 2)
def _capture2(input, env, reArg, flagsArg):
    for rx, glob in _regex_args(input, env, reArg, flagsArg):
        for m in _iter_matches(rx, input, glob):
            yield _capture_obj(m)


@_register("scan", 1)
def _scan1(input, env, reArg):
    for rx, _g in _regex_args(input, env, reArg, None):
        for m in _iter_matches(rx, input, True):
            if m.re.groups:
                yield [m.group(i) for i in range(1, m.re.groups + 1)]
            else:
                yield m.group()


@_register("scan", 2)
def _scan2(input, env, reArg, flagsArg):
    for rx, _g in _regex_args(input, env, reArg, flagsArg):
        for m in _iter_matches(rx, input, True):
            if m.re.groups:
                yield [m.group(i) for i in range(1, m.re.groups + 1)]
            else:
                yield m.group()


@_register("split", 2)
def _split2(input, env, reArg, flagsArg):
    for rx, _g in _regex_args(input, env, reArg, flagsArg):
        out = []
        last = 0
        for m in _iter_matches(rx, input, True):
            out.append(input[last:m.start()])
            last = m.end()
        out.append(input[last:])
        yield out


def _sub_impl(input, env, reArg, replArg, flagsArg, force_global):
    if not isinstance(input, str):
        raise JqError("%s cannot be matched, as it is not a string" % describe(input))
    for rx, glob in _regex_args(input, env, reArg, flagsArg):
        glob = glob or force_global
        matches = list(_iter_matches(rx, input, glob))
        if not matches:
            yield input
            continue

        def build(i, pos, acc):
            if i == len(matches):
                yield acc + input[pos:]
                return
            m = matches[i]
            prefix = acc + input[pos:m.start()]
            for repl in replArg.vals(_capture_obj(m)):
                if not isinstance(repl, str):
                    raise JqError("%s cannot be used as a replacement" % describe(repl))
                for r in build(i + 1, m.end(), prefix + repl):
                    yield r
        for r in build(0, 0, ""):
            yield r


@_register("sub", 2)
def _sub2(input, env, reArg, replArg):
    return _sub_impl(input, env, reArg, replArg, None, False)


@_register("sub", 3)
def _sub3(input, env, reArg, replArg, flagsArg):
    return _sub_impl(input, env, reArg, replArg, flagsArg, False)


@_register("gsub", 2)
def _gsub2(input, env, reArg, replArg):
    return _sub_impl(input, env, reArg, replArg, None, True)


@_register("gsub", 3)
def _gsub3(input, env, reArg, replArg, flagsArg):
    return _sub_impl(input, env, reArg, replArg, flagsArg, True)


# --- numbers -----------------------------------------------------------------

@_register("range", 1)
def _range1(input, env, n):
    for hi in n.vals(input):
        x = 0
        while x < hi:
            yield x
            x += 1


@_register("range", 2)
def _range2(input, env, lo, hi):
    for l in lo.vals(input):
        for h in hi.vals(input):
            x = l
            while x < h:
                yield x
                x += 1


@_register("range", 3)
def _range3(input, env, lo, hi, step):
    for l in lo.vals(input):
        for h in hi.vals(input):
            for s in step.vals(input):
                x = l
                if s > 0:
                    while x < h:
                        yield x
                        x += s
                elif s < 0:
                    while x > h:
                        yield x
                        x += s
    return


def _require_number(input, name):
    if isinstance(input, bool) or not isinstance(input, (int, float)):
        raise JqError("%s (%s) number required" % (type_name(input), encode(input)))
    return input


def _math0(name, fn):
    def impl(input, env):
        n = _require_number(input, name)
        if isinstance(n, float) and (math.isnan(n) or math.isinf(n)):
            yield n
            return
        yield fn(n)
    PY_BUILTINS[(name, 0)] = impl


def _round_half_away(x):
    return math.floor(x + 0.5) if x >= 0 else math.ceil(x - 0.5)


_math0("floor", lambda x: math.floor(x))
_math0("ceil", lambda x: math.ceil(x))
_math0("round", _round_half_away)
_math0("trunc", lambda x: math.trunc(x))
_math0("fabs", math.fabs)
_math0("sqrt", math.sqrt)
_math0("exp", math.exp)
_math0("exp2", lambda x: 2.0 ** x)
_math0("exp10", lambda x: 10.0 ** x)
_math0("log", math.log)
_math0("log2", math.log2)
_math0("log10", math.log10)
_math0("sin", math.sin)
_math0("cos", math.cos)
_math0("tan", math.tan)
_math0("asin", math.asin)
_math0("acos", math.acos)
_math0("atan", math.atan)
_math0("sinh", math.sinh)
_math0("cosh", math.cosh)
_math0("tanh", math.tanh)
_math0("asinh", math.asinh)
_math0("acosh", math.acosh)
_math0("atanh", math.atanh)
_math0("cbrt", lambda x: math.copysign(abs(x) ** (1.0 / 3.0), x))
_math0("significand", lambda x: math.frexp(x)[0] * 2 if x else 0.0)
_math0("logb", lambda x: float(math.frexp(x)[1] - 1) if x else -float("inf"))
_math0("gamma", math.lgamma)
_math0("lgamma", math.lgamma)
_math0("tgamma", math.gamma)
_math0("nearbyint", lambda x: float(round(x)))


@_register("pow", 2)
def _pow(input, env, a, b):
    for y in b.vals(input):
        for x in a.vals(input):
            yield _require_number(x, "pow") ** _require_number(y, "pow")


@_register("atan2", 2)
def _atan2(input, env, a, b):
    for y in b.vals(input):
        for x in a.vals(input):
            yield math.atan2(x, y)


@_register("have_decnum", 0)
def _have_decnum(input, env):
    # Python ints are arbitrary-precision, which matches jq-with-decnum
    # behavior for integer literals (exact round-tripping and comparison).
    yield True


@_register("last", 1)
def _last1(input, env, f):
    last = _SENTINEL = object()
    for v in f.vals(input):
        last = v
    if last is not _SENTINEL:
        yield last


@_register("tostream", 0)
def _tostream(input, env):
    def walk(v, path):
        if isinstance(v, list) and v:
            for i, item in enumerate(v):
                for ev in walk(item, path + [i]):
                    yield ev
            yield [path + [len(v) - 1]]
        elif isinstance(v, dict) and v:
            last_k = None
            for k, item in v.items():
                last_k = k
                for ev in walk(item, path + [k]):
                    yield ev
            yield [path + [last_k]]
        else:
            yield [path, v]
    for ev in walk(input, []):
        yield ev


@_register("fromstream", 1)
def _fromstream(input, env, f):
    x = None
    emit = False
    for ev in f.vals(input):
        if emit:
            x = None
            emit = False
        if not isinstance(ev, list) or not ev:
            raise JqError("Invalid streaming format")
        path = ev[0]
        if not isinstance(path, list):
            raise JqError("Invalid streaming format")
        if len(ev) == 2:
            if len(path) == 0:
                x = ev[1]
                emit = True
            else:
                x = ops.set_path(x, path, ev[1])
        else:
            if len(path) <= 1:
                emit = True
        if emit:
            yield x


@_register("truncate_stream", 1)
def _truncate_stream(input, env, f):
    n = _require_number(input, "truncate_stream")
    depth = int(n)
    for ev in f.vals(None):
        if not isinstance(ev, list) or not ev or not isinstance(ev[0], list):
            raise JqError("Invalid streaming format")
        if len(ev[0]) > depth:
            yield [ev[0][depth:]] + ev[1:]


@_register("have_literal_numbers", 0)
def _have_literal_numbers(input, env):
    yield True


@_register("toboolean", 0)
def _toboolean(input, env):
    if isinstance(input, bool):
        yield input
    elif input == "true":
        yield True
    elif input == "false":
        yield False
    else:
        raise JqError("%s cannot be parsed as a boolean" % describe(input))


@_register("bsearch", 1)
def _bsearch(input, env, target):
    arr = _require_array(input, "searched from")
    for t in target.vals(input):
        lo, hi = 0, len(arr)
        found = None
        while lo < hi:
            mid = (lo + hi) // 2
            c = jq_cmp(arr[mid], t)
            if c == 0:
                found = mid
                break
            if c < 0:
                lo = mid + 1
            else:
                hi = mid
        yield found if found is not None else -1 - lo


def _require_string(input, what):
    if not isinstance(input, str):
        raise JqError("%s cannot be %s" % (describe(input), what))
    return input


def _require_trim_string(input):
    if not isinstance(input, str):
        raise JqError("trim input must be a string")
    return input


@_register("trim", 0)
def _trim(input, env):
    yield _require_trim_string(input).strip()


@_register("ltrim", 0)
def _ltrim(input, env):
    yield _require_trim_string(input).lstrip()


@_register("rtrim", 0)
def _rtrim(input, env):
    yield _require_trim_string(input).rstrip()


@_register("_strindices", 1)
def _strindices(input, env, arg):
    if not isinstance(input, str):
        raise JqError("%s cannot be searched, as it is not a string" % describe(input))
    for s in arg.vals(input):
        if not isinstance(s, str):
            raise JqError("%s is not a string" % describe(s))
        yield ops.indices_impl(input, s)


@_register("infinite", 0)
def _infinite(input, env):
    yield float("inf")


@_register("nan", 0)
def _nan(input, env):
    yield float("nan")


@_register("isinfinite", 0)
def _isinfinite(input, env):
    yield isinstance(input, float) and math.isinf(input)


@_register("isnan", 0)
def _isnan(input, env):
    yield isinstance(input, float) and math.isnan(input)


@_register("isnormal", 0)
def _isnormal(input, env):
    n = _require_number(input, "isnormal")
    yield not (n == 0 or math.isnan(n) or math.isinf(n) or abs(n) < 2.2250738585072014e-308)


# --- arrays ---------------------------------------------------------------------

_CMP_KEY = functools.cmp_to_key(jq_cmp)


def _require_array(input, what):
    if not isinstance(input, list):
        raise JqError("%s cannot be %s" % (describe(input), what))
    return input


def _natively_sortable(values):
    """True when Python's own ordering of these values matches jq's
    (uniformly all strings, or all bool-free nan-free numbers)."""
    kind = None
    for v in values:
        t = type(v)
        if t is int:
            k = "n"
        elif t is float:
            if math.isnan(v):
                return False
            k = "n"
        elif t is str:
            k = "s"
        else:
            return False
        if kind is None:
            kind = k
        elif kind != k:
            return False
    return True


@_register("sort", 0)
def _sort(input, env):
    arr = _require_array(input, "sorted")
    if _natively_sortable(arr):
        yield sorted(arr)
    else:
        yield sorted(arr, key=_CMP_KEY)


def _by_key(f, x):
    g = f.g
    if g is not None:
        return [g(x, f.env)]
    return list(f.vals(x))


def _sorted_keyed(arr, f):
    """[(key, x)] sorted by jq order; key is the [f] list jq sorts by."""
    keyed = [(_by_key(f, x), x) for x in arr]
    if all(len(k) == 1 for k, _x in keyed) and _natively_sortable(
            [k[0] for k, _x in keyed]):
        keyed.sort(key=lambda kv: kv[0][0])
    else:
        keyed.sort(key=lambda kv: _CMP_KEY(kv[0]))
    return keyed


@_register("sort_by", 1)
def _sort_by(input, env, f):
    arr = _require_array(input, "sorted")
    yield [x for _k, x in _sorted_keyed(arr, f)]


@_register("group_by", 1)
def _group_by(input, env, f):
    arr = _require_array(input, "grouped")
    out = []
    cur_key = None
    cur = None
    for k, x in _sorted_keyed(arr, f):
        if cur is None or jq_cmp(k, cur_key) != 0:
            cur = [x]
            cur_key = k
            out.append(cur)
        else:
            cur.append(x)
    yield out


@_register("unique", 0)
def _unique(input, env):
    arr = _require_array(input, "sorted")
    if _natively_sortable(arr):
        arr = sorted(arr)
        out = []
        for x in arr:
            if not out or out[-1] != x:
                out.append(x)
        yield out
        return
    arr = sorted(arr, key=_CMP_KEY)
    out = []
    for x in arr:
        if not out or jq_cmp(out[-1], x) != 0:
            out.append(x)
    yield out


@_register("unique_by", 1)
def _unique_by(input, env, f):
    arr = _require_array(input, "sorted")
    out = []
    last_key = None
    for k, x in _sorted_keyed(arr, f):
        if not out or jq_cmp(k, last_key) != 0:
            out.append(x)
            last_key = k
    yield out


@_register("min", 0)
def _min(input, env):
    arr = _require_array(input, "examined")
    yield _extreme_by(arr, lambda x: x, False) if arr else None


@_register("max", 0)
def _max(input, env):
    arr = _require_array(input, "examined")
    yield _extreme_by(arr, lambda x: x, True) if arr else None


def _extreme_by(arr, keyfn, want_max):
    # jq tie-breaking: min keeps the first minimal element, max keeps the last
    # maximal one (both fall out of its reduce-based definitions).
    best = None
    best_key = None
    for x in arr:
        k = keyfn(x)
        if best_key is None:
            best, best_key = x, k
            continue
        c = jq_cmp(k, best_key)
        if (want_max and c >= 0) or (not want_max and c < 0):
            best, best_key = x, k
    return best


@_register("min_by", 1)
def _min_by(input, env, f):
    arr = _require_array(input, "examined")
    yield _extreme_by(arr, lambda x: _by_key(f, x), False) if arr else None


@_register("max_by", 1)
def _max_by(input, env, f):
    arr = _require_array(input, "examined")
    yield _extreme_by(arr, lambda x: _by_key(f, x), True) if arr else None


@_register("reverse", 0)
def _reverse(input, env):
    if isinstance(input, str):
        yield input[::-1]
    elif input is None:
        yield []
    else:
        yield list(reversed(_require_array(input, "reversed")))


def _flatten_into(arr, depth, out):
    for x in arr:
        if isinstance(x, list) and depth > 0:
            _flatten_into(x, depth - 1, out)
        else:
            out.append(x)


@_register("flatten", 0)
def _flatten0(input, env):
    out = []
    _flatten_into(_require_array(input, "flattened"), 1 << 62, out)
    yield out


@_register("flatten", 1)
def _flatten1(input, env, depth):
    for d in depth.vals(input):
        if d < 0:
            raise JqError("flatten depth must not be negative")
        out = []
        _flatten_into(_require_array(input, "flattened"), int(d), out)
        yield out


@_register("add", 0)
def _add(input, env):
    if input is None:
        yield None
        return
    if isinstance(input, dict):
        items = list(input.values())
    elif isinstance(input, list):
        items = input
    else:
        raise JqError("Cannot iterate over %s" % describe(input))
    acc = None
    for x in items:
        acc = ops.add_values(acc, x)
    yield acc


# --- dates -------------------------------------------------------------------------

def _to_struct(broken, who="strftime/1"):
    if not isinstance(broken, list) or len(broken) < 1:
        raise JqError("%s requires parsed datetime inputs" % who)
    for x in broken:
        if isinstance(x, bool) or not isinstance(x, (int, float)):
            raise JqError("%s requires parsed datetime inputs" % who)
    broken = list(broken) + [0] * (8 - len(broken))
    y, mo, d, h, mi, s = broken[0], broken[1], broken[2], broken[3], broken[4], broken[5]
    frac = s - int(s)
    wday = int(broken[6]) if len(broken) > 6 else 0
    yday = int(broken[7]) if len(broken) > 7 else 0
    return _time.struct_time((int(y), int(mo) + 1, int(d), int(h), int(mi), int(s),
                              (wday - 1) % 7, yday + 1, 0)), frac


def _from_struct(st, frac=0.0):
    sec = st.tm_sec + frac
    return [st.tm_year, st.tm_mon - 1, st.tm_mday, st.tm_hour, st.tm_min,
            sec if frac else st.tm_sec, (st.tm_wday + 1) % 7, st.tm_yday - 1]


@_register("now", 0)
def _now(input, env):
    yield _time.time()


@_register("gmtime", 0)
def _gmtime(input, env):
    n = _require_number(input, "gmtime")
    st = _time.gmtime(int(n))
    yield _from_struct(st, n - int(n))


@_register("localtime", 0)
def _localtime(input, env):
    n = _require_number(input, "localtime")
    st = _time.localtime(int(n))
    yield _from_struct(st, n - int(n))


@_register("mktime", 0)
def _mktime(input, env):
    st, _frac = _to_struct(input, "mktime")
    yield calendar.timegm(st)


@_register("strftime", 1)
def _strftime(input, env, fmt):
    for f in fmt.vals(input):
        if not isinstance(f, str):
            raise JqError("strftime/1 requires a string format")
        if isinstance(input, (int, float)) and not isinstance(input, bool):
            st = _time.gmtime(int(input))
        else:
            st, _frac = _to_struct(input)
        yield _time.strftime(f, st)


@_register("strflocaltime", 1)
def _strflocaltime(input, env, fmt):
    for f in fmt.vals(input):
        if not isinstance(f, str):
            raise JqError("strflocaltime/1 requires a string format")
        if isinstance(input, (int, float)) and not isinstance(input, bool):
            st = _time.localtime(int(input))
        else:
            st, _frac = _to_struct(input, "strflocaltime/1")
        yield _time.strftime(f, st)


@_register("strptime", 1)
def _strptime(input, env, fmt):
    for f in fmt.vals(input):
        if not isinstance(input, str):
            raise JqError("strptime/1 requires string inputs and arguments")
        try:
            st = _time.strptime(input, f)
        except ValueError as e:
            raise JqError("date \"%s\" does not match format \"%s\": %s" % (input, f, e))
        yield _from_struct(st)


# --- formats (@text, @json, ...) --------------------------------------------------


def _fmt_tostring(v):
    return v if isinstance(v, str) else encode(v)


_HTML_ESCAPES = {"&": "&amp;", "<": "&lt;", ">": "&gt;", "'": "&apos;", '"': "&quot;"}


def _csv_cell(v):
    if v is None:
        return ""
    if isinstance(v, str):
        return '"' + v.replace('"', '""') + '"'
    if isinstance(v, bool) or isinstance(v, (int, float)):
        return encode(v)
    raise JqError("%s is not valid in a csv row" % describe(v))


def _tsv_cell(v):
    if v is None:
        return ""
    if isinstance(v, str):
        return (v.replace("\\", "\\\\").replace("\t", "\\t")
                 .replace("\n", "\\n").replace("\r", "\\r"))
    if isinstance(v, bool) or isinstance(v, (int, float)):
        return encode(v)
    raise JqError("%s is not valid in a tsv row" % describe(v))


def _sh_quote(v):
    if isinstance(v, str):
        return "'" + v.replace("'", "'\\''") + "'"
    if isinstance(v, bool) or v is None or isinstance(v, (int, float)):
        return encode(v)
    raise JqError("%s can not be escaped for shell" % describe(v))


def apply_format(name, v):
    if name == "text":
        return _fmt_tostring(v)
    if name == "json":
        return encode(v)
    if name == "html":
        s = _fmt_tostring(v)
        return "".join(_HTML_ESCAPES.get(c, c) for c in s)
    if name == "uri":
        return urllib.parse.quote(_fmt_tostring(v), safe="-_.~")
    if name == "urid":
        s = urllib.parse.unquote(_fmt_tostring(v), errors="strict")
        return s
    if name == "csv":
        if not isinstance(v, list):
            raise JqError("%s cannot be csv-formatted, only an array can be" % describe(v))
        return ",".join(_csv_cell(c) for c in v)
    if name == "tsv":
        if not isinstance(v, list):
            raise JqError("%s cannot be tsv-formatted, only an array can be" % describe(v))
        return "\t".join(_tsv_cell(c) for c in v)
    if name == "sh":
        if isinstance(v, list):
            return " ".join(_sh_quote(c) for c in v)
        return _sh_quote(v)
    if name == "base64":
        return base64.b64encode(_fmt_tostring(v).encode("utf-8")).decode("ascii")
    if name == "base64d":
        s = _fmt_tostring(v)
        pad = (-len(s)) % 4
        try:
            return base64.b64decode(s + "=" * pad).decode("utf-8", "replace")
        except Exception:
            raise JqError("%s is not valid base64 data" % describe(v))
    raise JqError("%s is not a valid format" % name)
