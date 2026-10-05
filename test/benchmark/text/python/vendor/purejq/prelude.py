"""Builtins defined in jq itself, mirroring jq's own builtin.jq approach."""
from __future__ import annotations

import os

from .compiler import Env, FuncVal
from .parser import parse

PRELUDE = r"""
def values: select(. != null);
def nulls: select(. == null);
def booleans: select(type == "boolean");
def numbers: select(type == "number");
def strings: select(type == "string");
def arrays: select(type == "array");
def objects: select(type == "object");
def iterables: select(type == "array" or type == "object");
def scalars: select(type != "array" and type != "object");
def recurse(f): def r: ., (f | r); r;
def recurse(f; cond): def r: ., (f | select(cond) | r); r;
def recurse: recurse(.[]?);
def to_entries: [keys_unsorted[] as $k | {key: $k, value: .[$k]}];
def from_entries:
  reduce .[] as $x ({};
    . + {($x | if type == "object"
               then (.key? // .k? // .name? // .Name? // .Key? // .K? // null
                     | if type == "string" then . else tojson end)
               else tojson end):
         ($x | if type == "object"
               then (if has("value") then .value
                     elif has("v") then .v
                     elif has("Value") then .Value
                     elif has("V") then .V
                     else null end)
               else null end)});
def with_entries(f): to_entries | map(f) | from_entries;
def map_values(f): .[] |= f;
def paths: path(..) | select(length > 0);
def paths(node_filter): . as $dot | paths | select(. as $p | $dot | getpath($p) | node_filter);
def leaf_paths: paths(scalars);
def first(f): label $out | f | ., break $out;
def isempty(g): label $go | (g | false, break $go), true;
def any(g; f): isempty(first(g | f | select(.))) | not;
def all(g; f): isempty(first(g | f | select(. | not)));
def any(f): any(.[]; f);
def all(f): all(.[]; f);
def any: any(.);
def all: all(.);
def in(xs): . as $x | xs | has($x);
def inside(xs): . as $x | xs | contains($x);
def first: .[0];
def last: .[-1];
def add(f): reduce f as $x (null; . + $x);
def skip($n; f):
  if $n < 0 then error("skip doesn't support negative count")
  else foreach f as $item (0; . + 1; if . > $n then $item else empty end) end;
def trimstr($s): ltrimstr($s) | rtrimstr($s);
def isvalid(f): try (f | true) catch false;
def error($msg): $msg | error;
def del(f): delpaths([path(f)]);
def abs: if type == "number" and . < 0 then - . else . end;
def until(cond; update): def _u: if cond then . else (update | _u) end; _u;
def while(cond; update): def _w: if cond then ., (update | _w) else empty end; _w;
def repeat(f): def _r: f | (., _r); _r;
def limit($n; f):
  if $n > 0 then
    label $out | foreach f as $item (0; . + 1; $item, if . >= $n then break $out else empty end)
  elif $n == 0 then empty
  else error("limit doesn't support negative count") end;
def nth($n): .[$n];
def nth($n; f):
  if $n < 0 then error("nth doesn't support negative indices")
  else label $out | foreach f as $item ($n; . - 1; if . < 0 then $item, break $out else empty end) end;
def index($i): indices($i) | .[0];
def rindex($i): indices($i) | .[-1:][0];
def combinations: if length == 0 then [] else .[0][] as $x | (.[1:] | combinations) as $w | [$x] + $w end;
def combinations(n): . as $dot | [range(n)] | map($dot) | combinations;
def walk(f): def w: if type == "object" then map_values(w) elif type == "array" then map(w) else . end | f; w;
def transpose:
  if . == [] then []
  else . as $in
     | (map(length) | max) as $max
     | [range(0; $max) as $j | [range(0; $in | length) as $i | $in[$i][$j]]]
  end;
def splits($re): splits($re; null);
def splits($re; flags): split($re; flags) | .[];
def ascii: [.] | implode;
def todate: strftime("%Y-%m-%dT%H:%M:%SZ");
def fromdate: strptime("%Y-%m-%dT%H:%M:%SZ") | mktime;
def todateiso8601: strftime("%Y-%m-%dT%H:%M:%SZ");
def fromdateiso8601: strptime("%Y-%m-%dT%H:%M:%SZ") | mktime;
def dateadd(u; n): . + n;
def datesub(u; n): . - n;
def date: todate;
def toarray: if type == "array" then . else [.] end;
def debug($msg): ($msg | debug | empty), .;
def IN(s): any(s == .; .);
def IN(src; s): any(src == s; .);
def INDEX(stream; idx_expr): reduce stream as $row ({}; .[$row | idx_expr | tostring] |= $row);
def INDEX(idx_expr): INDEX(.[]; idx_expr);
def JOIN($idx; idx_expr): [.[] | [., $idx[idx_expr]]];
def JOIN($idx; stream; idx_expr): stream | [., $idx[idx_expr]];
def JOIN($idx; stream; idx_expr; join_expr): stream | [., $idx[idx_expr]] | join_expr;
def GROUP_BY(f): group_by(f);
def UNIQUE_BY(f): unique_by(f);
def pick(pathexps): . as $top | reduce path(pathexps) as $p (null; setpath($p; $top | getpath($p)));
.
"""

_PRELUDE_ENV = None
_PRELUDE_NAMES = []


def prelude_env():
    """Build (once) the root environment containing all prelude definitions."""
    global _PRELUDE_ENV
    if _PRELUDE_ENV is None:
        from .compiler import collect_defined_names
        node = parse(PRELUDE)
        ctx = frozenset(collect_defined_names(node))
        funcs = {}
        created = []
        while node[0] == "funcdef":
            _, name, params, body, rest = node
            fv = FuncVal(params, body, ctx)
            funcs[(name, len(params))] = fv
            created.append(fv)
            _PRELUDE_NAMES.append("%s/%d" % (name, len(params)))
            node = rest
        root = Env(vars={"ENV": dict(os.environ), "__prog_args": {}}, funcs=funcs)
        for fv in created:
            fv.env = root
        _PRELUDE_ENV = root
    return _PRELUDE_ENV


def prelude_names():
    prelude_env()
    return list(_PRELUDE_NAMES)
