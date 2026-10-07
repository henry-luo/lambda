"""Tokenizer for jq programs, including string interpolation."""
from __future__ import annotations

import re

from .errors import JqParseError

KEYWORDS = frozenset([
    "def", "as", "if", "then", "elif", "else", "end",
    "reduce", "foreach", "try", "catch", "label",
    "and", "or", "import", "include",
])

_TOKEN_RE = re.compile(r"""
  (?P<ws>[ \t\r\n]+)
| (?P<comment>\#[^\n]*)
| (?P<num>(?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?)
| (?P<op>\?//|//=|\|=|\+=|-=|\*=|/=|%=|==|!=|<=|>=|//|\.\.)
| (?P<ident>[a-zA-Z_][a-zA-Z0-9_]*(?:::[a-zA-Z_][a-zA-Z0-9_]*)?)
| (?P<var>\$(?:__loc__|ENV|[a-zA-Z_][a-zA-Z0-9_]*))
| (?P<format>@[a-zA-Z0-9_]+)
| (?P<punct>[.\[\]{}()|,:;=<>+\-*/%?$])
""", re.X)

_ESCAPES = {'"': '"', "\\": "\\", "/": "/", "b": "\b",
            "f": "\f", "n": "\n", "r": "\r", "t": "\t"}

_INT_RE = re.compile(r"[0-9]+$")
_FIELD_RE = re.compile(r"\.([a-zA-Z_][a-zA-Z0-9_]*)")


def lex(source):
    """Return a list of (kind, value, pos) tokens ending with ('EOF', None, len)."""
    tokens = []
    i = 0
    n = len(source)
    while i < n:
        ch = source[i]
        if ch == '"':
            parts, i = _lex_string(source, i)
            tokens.append(("STR", parts, i))
            continue
        if ch == ".":
            # `.name` field access. _FIELD_RE only matches ASCII identifiers;
            # a bare "." or ".<non-identifier>" (including Unicode letters like
            # ".ä") falls through to be lexed as the "." operator, then errors
            # if what follows is not valid - never crashes on a None match.
            m = _FIELD_RE.match(source, i)
            if m is not None:
                tokens.append(("FIELD", m.group(1), m.end()))
                i = m.end()
                continue
        m = _TOKEN_RE.match(source, i)
        if m is None:
            raise JqParseError("Unexpected character %r at position %d" % (ch, i))
        kind = m.lastgroup
        text = m.group()
        i = m.end()
        if kind in ("ws", "comment"):
            continue
        if kind == "num":
            if _INT_RE.match(text):
                tokens.append(("NUM", int(text), i))
            else:
                tokens.append(("NUM", float(text), i))
        elif kind == "ident":
            if text in KEYWORDS:
                tokens.append(("KW", text, i))
            else:
                tokens.append(("IDENT", text, i))
        elif kind == "var":
            tokens.append(("VAR", text[1:], i))
        elif kind == "format":
            tokens.append(("FORMAT", text[1:], i))
        else:  # op / punct
            tokens.append(("OP", text, i))
    tokens.append(("EOF", None, n))
    return tokens


def _hex4(source, i, n):
    """Parse exactly 4 hex digits at source[i:i+4]; JqParseError if invalid."""
    if i + 4 > n:
        raise JqParseError("Invalid \\u escape")
    digits = source[i:i + 4]
    try:
        return int(digits, 16)
    except ValueError:
        raise JqParseError("Invalid \\u escape: %r is not 4 hex digits"
                           % digits) from None


def _lex_string(source, i):
    """Lex a double-quoted string starting at source[i].

    Returns (parts, next_index) where parts is a list whose items are either
    literal `str` fragments or ("interp", tokens) for \\(...) interpolations.
    """
    assert source[i] == '"'
    i += 1
    n = len(source)
    parts = []
    buf = []

    def flush():
        if buf:
            parts.append("".join(buf))
            del buf[:]

    while True:
        if i >= n:
            raise JqParseError("Unterminated string literal")
        ch = source[i]
        if ch == '"':
            flush()
            return parts, i + 1
        if ch != "\\":
            buf.append(ch)
            i += 1
            continue
        if i + 1 >= n:
            raise JqParseError("Unterminated string literal")
        esc = source[i + 1]
        if esc in _ESCAPES:
            buf.append(_ESCAPES[esc])
            i += 2
        elif esc == "u":
            code = _hex4(source, i + 2, n)
            i += 6
            if 0xD800 <= code <= 0xDBFF and source[i:i + 2] == "\\u":
                low = _hex4(source, i + 2, n)
                if 0xDC00 <= low <= 0xDFFF:
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00)
                    i += 6
            buf.append(chr(code))
        elif esc == "(":
            flush()
            expr, i = _scan_interp(source, i + 2)
            parts.append(("interp", lex(expr)))
        else:
            raise JqParseError("Invalid escape sequence \\%s in string" % esc)


def _scan_interp(source, i):
    """Scan an interpolation body starting after '\\('. Returns (text, index_after_paren)."""
    depth = 1
    start = i
    n = len(source)
    while i < n:
        ch = source[i]
        if ch == '"':
            # skip nested string literal, honoring escapes
            i += 1
            while i < n and source[i] != '"':
                i += 2 if source[i] == "\\" else 1
            i += 1
            continue
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return source[start:i], i + 1
        i += 1
    raise JqParseError("Unterminated string interpolation")
