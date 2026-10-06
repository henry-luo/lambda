"""jq-compatible JSON encoding (compact and pretty)."""
from __future__ import annotations

import math

from .errors import JqError

_ESCAPES = {
    '"': '\\"',
    "\\": "\\\\",
    "\b": "\\b",
    "\f": "\\f",
    "\n": "\\n",
    "\r": "\\r",
    "\t": "\\t",
}

_MAX_DOUBLE = "1.7976931348623157e+308"


def _escape_string(s, ascii=False):
    out = ['"']
    for ch in s:
        esc = _ESCAPES.get(ch)
        if esc is not None:
            out.append(esc)
        elif ch < " " or ch == "\x7f":
            out.append("\\u%04x" % ord(ch))
        elif ascii and ch > "\x7f":
            cp = ord(ch)
            if cp > 0xFFFF:  # encode astral chars as a UTF-16 surrogate pair
                cp -= 0x10000
                out.append("\\u%04x\\u%04x" % (0xD800 + (cp >> 10), 0xDC00 + (cp & 0x3FF)))
            else:
                out.append("\\u%04x" % cp)
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def format_number(v):
    if isinstance(v, int):
        return str(v)
    if math.isnan(v):
        return "null"
    if math.isinf(v):
        return _MAX_DOUBLE if v > 0 else "-" + _MAX_DOUBLE
    if v.is_integer() and abs(v) < 1e17:
        return str(int(v))
    return repr(v)


MAX_PRINT_DEPTH = 10000


def encode(v, sort_keys=False, ascii=False):
    """Encode a JSON value the way jq's compact output (-c) does.

    The default (no options) path is kept allocation-light because the engine
    leans on it heavily (tojson, error messages). sort_keys/ascii take a
    separate, options-aware path used by the CLI's -S/-a flags.
    """
    out = []
    if sort_keys or ascii:
        _enc_opts(v, out, 0, sort_keys, ascii)
    else:
        _enc(v, out, 0)
    return "".join(out)


def _enc(v, out, depth):
    if v is None:
        out.append("null")
    elif v is True:
        out.append("true")
    elif v is False:
        out.append("false")
    elif isinstance(v, (int, float)):
        out.append(format_number(v))
    elif isinstance(v, str):
        out.append(_escape_string(v))
    elif depth > MAX_PRINT_DEPTH:
        out.append('"<skipped: too deep>"')
    elif isinstance(v, list):
        out.append("[")
        for i, item in enumerate(v):
            if i:
                out.append(",")
            _enc(item, out, depth + 1)
        out.append("]")
    elif isinstance(v, dict):
        out.append("{")
        first = True
        for k, item in v.items():
            if not first:
                out.append(",")
            first = False
            if not isinstance(k, str):
                raise JqError("Object keys must be strings")
            out.append(_escape_string(k))
            out.append(":")
            _enc(item, out, depth + 1)
        out.append("}")
    else:
        raise JqError("Cannot encode value of type %s" % type(v).__name__)


def _enc_opts(v, out, depth, sort_keys, ascii):
    if v is None:
        out.append("null")
    elif v is True:
        out.append("true")
    elif v is False:
        out.append("false")
    elif isinstance(v, (int, float)):
        out.append(format_number(v))
    elif isinstance(v, str):
        out.append(_escape_string(v, ascii))
    elif depth > MAX_PRINT_DEPTH:
        out.append('"<skipped: too deep>"')
    elif isinstance(v, list):
        out.append("[")
        for i, item in enumerate(v):
            if i:
                out.append(",")
            _enc_opts(item, out, depth + 1, sort_keys, ascii)
        out.append("]")
    elif isinstance(v, dict):
        out.append("{")
        items = sorted(v.items()) if sort_keys else v.items()
        first = True
        for k, item in items:
            if not first:
                out.append(",")
            first = False
            if not isinstance(k, str):
                raise JqError("Object keys must be strings")
            out.append(_escape_string(k, ascii))
            out.append(":")
            _enc_opts(item, out, depth + 1, sort_keys, ascii)
        out.append("}")
    else:
        raise JqError("Cannot encode value of type %s" % type(v).__name__)


def encode_pretty(v, indent=2, sort_keys=False, ascii=False, tab=False):
    """Encode a JSON value the way jq's default pretty output does.

    `indent` is the number of spaces per level (jq default 2); `tab` uses a
    tab per level instead, matching jq's --tab.
    """
    unit = "\t" if tab else " " * indent
    out = []
    _enc_pretty(v, out, unit, 0, sort_keys, ascii)
    return "".join(out)


def _enc_pretty(v, out, unit, depth, sort_keys, ascii):
    if isinstance(v, list) and v:
        pad = unit * (depth + 1)
        out.append("[\n")
        for i, item in enumerate(v):
            if i:
                out.append(",\n")
            out.append(pad)
            _enc_pretty(item, out, unit, depth + 1, sort_keys, ascii)
        out.append("\n" + unit * depth + "]")
    elif isinstance(v, dict) and v:
        pad = unit * (depth + 1)
        out.append("{\n")
        items = sorted(v.items()) if sort_keys else v.items()
        first = True
        for k, item in items:
            if not first:
                out.append(",\n")
            first = False
            if not isinstance(k, str):
                raise JqError("Object keys must be strings")
            out.append(pad)
            out.append(_escape_string(k, ascii))
            out.append(": ")
            _enc_pretty(item, out, unit, depth + 1, sort_keys, ascii)
        out.append("\n" + unit * depth + "}")
    elif ascii and isinstance(v, str):
        out.append(_escape_string(v, ascii))
    else:
        _enc(v, out, depth)
