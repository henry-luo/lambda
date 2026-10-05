"""Recursive-descent parser producing tuple-based AST nodes.

Operator precedence (loosest to tightest), following jq's grammar:
  |   ,   //   = |= += -= *= /= %= //=   or   and   == != < <= > >=   + -   * / %   postfix
"""
from __future__ import annotations

from .errors import JqParseError
from .lexer import lex

_ASSIGN_OPS = frozenset(["=", "|=", "+=", "-=", "*=", "/=", "%=", "//="])
_CMP_OPS = frozenset(["==", "!=", "<", "<=", ">", ">="])
_LITERALS = {"true": True, "false": False, "null": None}

# Cap on parser recursion. Both structural nesting ((), [], {}) and pipe
# chains re-enter parse_pipe, so this one counter bounds them. Nesting costs
# ~13 interpreter frames per level, so 64 trips at ~830 frames - comfortably
# under CPython's default 1000 limit on every platform (including Windows'
# ~1 MB stack), yet far above any real jq program. Adversarial input like
# "[[[[..." raises a clean JqParseError instead of overflowing the C stack.
MAX_DEPTH = 64


def parse(source):
    p = Parser(lex(source))
    return p.parse_program()


class Parser:
    def __init__(self, tokens):
        self.toks = tokens
        self.pos = 0
        # >0 while parsing a reduce/foreach source, whose trailing `as`
        # belongs to the construct itself rather than to a binding.
        self.no_as = 0
        self.depth = 0  # current parse_pipe recursion depth (bounded below)

    # --- token helpers -------------------------------------------------
    def peek(self):
        return self.toks[self.pos]

    def next(self):
        t = self.toks[self.pos]
        self.pos += 1
        return t

    def at(self, kind, value=None):
        t = self.toks[self.pos]
        return t[0] == kind and (value is None or t[1] == value)

    def accept(self, kind, value=None):
        if self.at(kind, value):
            return self.next()
        return None

    def expect(self, kind, value=None):
        t = self.next()
        if t[0] != kind or (value is not None and t[1] != value):
            raise JqParseError("Expected %s %r but got %s %r at position %s"
                              % (kind, value, t[0], t[1], t[2]))
        return t

    # --- entry points ---------------------------------------------------
    def parse_program(self):
        node = self.parse_pipe()
        if not self.at("EOF"):
            t = self.peek()
            raise JqParseError("Unexpected token %r at position %s" % (t[1], t[2]))
        return node

    def parse_pipe(self):
        saved = self.no_as
        self.no_as = 0
        try:
            if self.at("KW", "def"):
                return self.parse_funcdef()
            left = self.parse_comma()
            if self.accept("OP", "|"):
                return ("pipe", left, self.parse_pipe())
            return left
        finally:
            self.no_as = saved

    def parse_funcdef(self):
        self.expect("KW", "def")
        name = self.expect("IDENT")[1]
        params = []
        if self.accept("OP", "("):
            while True:
                if self.at("VAR"):
                    params.append("$" + self.next()[1])
                else:
                    params.append(self.expect("IDENT")[1])
                if not self.accept("OP", ";"):
                    break
            self.expect("OP", ")")
        self.expect("OP", ":")
        body = self.parse_pipe()
        self.expect("OP", ";")
        rest = self.parse_pipe()
        return ("funcdef", name, params, body, rest)

    def parse_comma(self):
        left = self.parse_alt()
        while self.accept("OP", ","):
            left = ("comma", left, self.parse_alt())
        return left

    def parse_alt(self):
        left = self._maybe_as(self.parse_assign())
        if self.accept("OP", "//"):
            return ("alt", left, self.parse_alt())
        return left

    def _maybe_as(self, node):
        # `EXPR as $x | body` binds at this precedence: `1 + 2 as $x | ...`
        # captures the whole sum, but commas and reduce/foreach sources don't.
        if self.no_as == 0 and self.at("KW", "as"):
            self.next()
            patterns = self.parse_patterns()
            self.expect("OP", "|")
            body = self.parse_pipe()
            return ("as", node, patterns, body)
        return node

    def parse_assign(self):
        left = self.parse_or()
        t = self.peek()
        if t[0] == "OP" and t[1] in _ASSIGN_OPS:
            op = self.next()[1]
            right = self.parse_or()
            return ("assign", op, left, right)
        return left

    def parse_or(self):
        left = self.parse_and()
        while self.at("KW", "or"):
            self.next()
            left = ("or", left, self.parse_and())
        return left

    def parse_and(self):
        left = self.parse_cmp()
        while self.at("KW", "and"):
            self.next()
            left = ("and", left, self.parse_cmp())
        return left

    def parse_cmp(self):
        left = self.parse_add()
        t = self.peek()
        if t[0] == "OP" and t[1] in _CMP_OPS:
            op = self.next()[1]
            return ("binop", op, left, self.parse_add())
        return left

    def parse_add(self):
        left = self.parse_mul()
        while True:
            if self.accept("OP", "+"):
                left = ("binop", "+", left, self.parse_mul())
            elif self.accept("OP", "-"):
                left = ("binop", "-", left, self.parse_mul())
            else:
                return left

    def parse_mul(self):
        left = self.parse_unary()
        while True:
            t = self.peek()
            if t[0] == "OP" and t[1] in ("*", "/", "%"):
                op = self.next()[1]
                left = ("binop", op, left, self.parse_unary())
            else:
                return left

    def parse_unary(self, allow_as=True):
        if self.accept("OP", "-"):
            return ("neg", self.parse_unary(allow_as=False))
        return self.parse_postfix(allow_as=allow_as)

    # --- postfix chains and `as` bindings --------------------------------
    def parse_postfix(self, allow_as=True):
        node = self.parse_primary()
        while True:
            if self.at("FIELD"):
                node = ("field", node, self.next()[1])
                continue
            if self.at("OP", "."):
                nxt = self.toks[self.pos + 1]
                if nxt[0] == "STR":
                    self.next()
                    node = ("index", node, self._str_node(self.next()[1], None))
                    continue
                if nxt[0] == "OP" and nxt[1] == "[":  # `.a.[]` / `.a.[0]` (jq 1.8)
                    self.next()
                    continue
            if self.at("OP", "["):
                self.next()
                node = self._parse_bracket(node)
                continue
            if self.at("OP", "?"):
                self.next()
                node = ("opt", node)
                continue
            break
        return node

    def _parse_bracket(self, base):
        if self.accept("OP", "]"):
            return ("iterate", base)
        if self.accept("OP", ":"):
            hi = self.parse_pipe()
            self.expect("OP", "]")
            return ("slice", base, None, hi)
        idx = self.parse_pipe()
        if self.accept("OP", ":"):
            if self.accept("OP", "]"):
                return ("slice", base, idx, None)
            hi = self.parse_pipe()
            self.expect("OP", "]")
            return ("slice", base, idx, hi)
        self.expect("OP", "]")
        return ("index", base, idx)

    # --- primary expressions ---------------------------------------------
    def parse_primary(self):
        # Every structural descent - (...), [...], {...}, and object values -
        # bottoms out here and re-enters here for nested content, so guarding
        # parse_primary bounds the deep, frame-expensive recursion that would
        # otherwise overflow the C stack on input like "[[[[..." or "{a:{a:".
        self.depth += 1
        if self.depth > MAX_DEPTH:
            self.depth -= 1
            raise JqParseError("program nests too deeply (over %d levels)"
                               % MAX_DEPTH)
        try:
            return self._parse_primary()
        finally:
            self.depth -= 1

    def _parse_primary(self):
        t = self.peek()
        kind, value = t[0], t[1]

        if kind == "FIELD":
            self.next()
            return ("field", ("identity",), value)

        if kind == "OP":
            if value == ".":
                self.next()
                if self.at("STR"):
                    return ("index", ("identity",), self._str_node(self.next()[1], None))
                return ("identity",)
            if value == "..":
                self.next()
                return ("call", "recurse", [])
            if value == "(":
                self.next()
                node = self.parse_pipe()
                self.expect("OP", ")")
                return node
            if value == "[":
                self.next()
                if self.accept("OP", "]"):
                    return ("emptyarray",)
                node = self.parse_pipe()
                self.expect("OP", "]")
                return ("collect", node)
            if value == "{":
                self.next()
                return self.parse_object()
            if value == "$":
                self.next()
                self.expect("IDENT", "__loc__")
                return ("loc",)
            raise JqParseError("Unexpected token %r at position %s" % (value, t[2]))

        if kind == "NUM":
            self.next()
            return ("const", value)

        if kind == "STR":
            self.next()
            return self._str_node(value, None)

        if kind == "VAR":
            self.next()
            if value == "__loc__":
                return ("loc",)
            return ("var", value)

        if kind == "FORMAT":
            self.next()
            if self.at("STR"):
                return self._str_node(self.next()[1], value)
            return ("format", value)

        if kind == "KW":
            if value == "if":
                return self.parse_if()
            if value == "try":
                self.next()
                body = self.parse_unary(allow_as=False)
                handler = None
                if self.accept("KW", "catch"):
                    handler = self.parse_unary(allow_as=False)
                return ("try", body, handler)
            if value == "reduce":
                self.next()
                self.no_as += 1
                src = self.parse_or()
                self.no_as -= 1
                self.expect("KW", "as")
                patterns = self.parse_patterns()
                self.expect("OP", "(")
                init = self.parse_pipe()
                self.expect("OP", ";")
                update = self.parse_pipe()
                self.expect("OP", ")")
                return ("reduce", src, patterns, init, update)
            if value == "foreach":
                self.next()
                self.no_as += 1
                src = self.parse_or()
                self.no_as -= 1
                self.expect("KW", "as")
                patterns = self.parse_patterns()
                self.expect("OP", "(")
                init = self.parse_pipe()
                self.expect("OP", ";")
                update = self.parse_pipe()
                extract = None
                if self.accept("OP", ";"):
                    extract = self.parse_pipe()
                self.expect("OP", ")")
                return ("foreach", src, patterns, init, update, extract)
            if value == "label":
                self.next()
                name = self.expect("VAR")[1]
                self.expect("OP", "|")
                return ("label", name, self.parse_pipe())
            if value == "def":
                return self.parse_funcdef()
            raise JqParseError("Unexpected keyword %r at position %s" % (value, t[2]))

        if kind == "IDENT":
            self.next()
            if value in _LITERALS:
                return ("const", _LITERALS[value])
            if value == "break":
                name = self.expect("VAR")[1]
                return ("break", name)
            args = []
            if self.accept("OP", "("):
                while True:
                    args.append(self.parse_pipe())
                    if not self.accept("OP", ";"):
                        break
                self.expect("OP", ")")
            return ("call", value, args)

        raise JqParseError("Unexpected token %r at position %s" % (value, t[2]))

    def parse_if(self):
        self.expect("KW", "if")
        branches = []
        cond = self.parse_pipe()
        self.expect("KW", "then")
        branches.append((cond, self.parse_pipe()))
        while self.accept("KW", "elif"):
            cond = self.parse_pipe()
            self.expect("KW", "then")
            branches.append((cond, self.parse_pipe()))
        els = None
        if self.accept("KW", "else"):
            els = self.parse_pipe()
        self.expect("KW", "end")
        return ("if", branches, els)

    def parse_object(self):
        entries = []
        if self.accept("OP", "}"):
            return ("object", entries)
        while True:
            entries.append(self._parse_object_entry())
            if not self.accept("OP", ","):
                break
        self.expect("OP", "}")
        return ("object", entries)

    def _parse_object_entry(self):
        t = self.peek()
        kind, value = t[0], t[1]
        if kind in ("IDENT", "KW"):
            self.next()
            if self.accept("OP", ":"):
                return (("const", value), self._parse_object_value())
            return (("const", value), ("field", ("identity",), value))
        if kind == "VAR":
            self.next()
            if self.accept("OP", ":"):
                # {$y: v}: the key is $y's value, not the literal name
                return (("var", value), self._parse_object_value())
            if value == "__loc__":
                return (("const", "__loc__"), ("loc",))
            return (("const", value), ("var", value))
        if kind == "STR":
            self.next()
            key = self._str_node(value, None)
            if self.accept("OP", ":"):
                return (key, self._parse_object_value())
            return (key, ("index", ("identity",), key))
        if kind == "FORMAT":
            self.next()
            key = self._str_node(self.expect("STR")[1], value)
            self.expect("OP", ":")
            return (key, self._parse_object_value())
        if kind == "OP" and value == "(":
            self.next()
            key = self.parse_pipe()
            self.expect("OP", ")")
            self.expect("OP", ":")
            return (key, self._parse_object_value())
        raise JqParseError("Unexpected token %r in object at position %s" % (value, t[2]))

    def _parse_object_value(self):
        left = self.parse_alt()
        while self.accept("OP", "|"):
            left = ("pipe", left, self.parse_alt())
        return left

    # --- patterns ----------------------------------------------------------
    def parse_patterns(self):
        patterns = [self.parse_pattern()]
        while self.accept("OP", "?//"):
            patterns.append(self.parse_pattern())
        return patterns

    def parse_pattern(self):
        if self.at("VAR"):
            return ("pvar", self.next()[1])
        if self.accept("OP", "["):
            items = [self.parse_pattern()]
            while self.accept("OP", ","):
                items.append(self.parse_pattern())
            self.expect("OP", "]")
            return ("parray", items)
        if self.accept("OP", "{"):
            entries = []
            while True:
                entries.append(self._parse_pattern_entry())
                if not self.accept("OP", ","):
                    break
            self.expect("OP", "}")
            return ("pobject", entries)
        t = self.peek()
        raise JqParseError("Expected pattern at position %s, got %r" % (t[2], t[1]))

    def _parse_pattern_entry(self):
        t = self.peek()
        kind, value = t[0], t[1]
        if kind == "VAR":
            self.next()
            if self.accept("OP", ":"):
                # {$b: pattern} binds $b to the value at "b" AND destructures it
                return (("const", value), self.parse_pattern(), value)
            return (("const", value), ("pvar", value), None)
        if kind in ("IDENT", "KW"):
            self.next()
            self.expect("OP", ":")
            return (("const", value), self.parse_pattern(), None)
        if kind == "STR":
            self.next()
            key = self._str_node(value, None)
            self.expect("OP", ":")
            return (key, self.parse_pattern(), None)
        if kind == "OP" and value == "(":
            self.next()
            key = self.parse_pipe()
            self.expect("OP", ")")
            self.expect("OP", ":")
            return (key, self.parse_pattern(), None)
        raise JqParseError("Unexpected token %r in pattern at position %s" % (value, t[2]))

    # --- helpers --------------------------------------------------------------
    def _str_node(self, parts, fmt):
        out = []
        for part in parts:
            if isinstance(part, str):
                out.append(part)
            else:
                sub = Parser(part[1])
                out.append(sub.parse_program())
        if fmt is None and len(out) == 1 and isinstance(out[0], str):
            return ("const", out[0])
        if fmt is None and not out:
            return ("const", "")
        return ("str", fmt, out)
