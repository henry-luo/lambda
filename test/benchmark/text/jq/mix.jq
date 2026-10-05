# jq_mix: breadth workload. jaq's examples/benches filters (MIT), each reduced
# to an integer digest; n values are cut down from jaq's (see the proposal).
def rounds: 7;
# jq 1.7.1 src/builtin.jq definition: jqjs has no repeat/1
def repeat(f): def _repeat: f, _repeat; _repeat;
def digest: if type == "array" or type == "object" or type == "string" then length
  elif type == "number" then . elif type == "boolean" then (if . then 1 else 0 end) else 0 end;
def b_upto: def upto($max): if . < $max then ., (.+1 | upto($max)) end; . as $max | 0 | [upto($max)];
def b_reduce_update: reduce range(.) as $x ([[]]; .[0] += [$x]);
def b_reverse: [range(.)] | reverse;
def b_sort: [range(.) | 0 - .] | sort;
def b_group_by: [range(0; .)] | group_by(. % 2);
def b_min_max: [range(.)] | [min, max] | add;
def b_add: [range(.) | [.]] | add;
def b_kv: [range(.) | {(tostring): .}] | add;
def b_kv_update: [range(.) | {(tostring): .}] | add | .[] += 1;
def b_kv_entries: [range(.) | {(tostring): .}] | add | with_entries(.value += 1);
def b_ex_implode: [limit(.; repeat("a"))] | add | explode | implode;
def b_reduce: reduce range(.) as $x (0; . + $x);
def b_try_catch: [range(.) | try error catch .];
def b_repeat: [limit(.; repeat(1))];
def b_from: [limit(.; 0 | recurse(.+1))];
def b_last: last(range(.));
def b_pyramid: def pyramid($max): def rec: if . < $max then ., (.+1 | rec), . end; rec; . as $max | 0 | [pyramid($max)] | length;
def b_tree_contains: nth(.; 0 | recurse([., .])) | [contains(.)];
def b_tree_flatten: nth(.; 0 | recurse([., .])) | flatten;
def b_tree_update: nth(.; 0 | recurse([., .])) | (.. | scalars) |= .+1;
def b_tree_paths: nth(.; 0 | recurse([., .])) | [paths];
def b_to_fromjson: [range(.) | tojson] | join(",") | "[" + . + "]" | fromjson;
def b_ack: def ack($m; $n):
  if $m == 0 then $n + 1
  elif $n == 0 then ack($m-1; 1)
  else ack($m-1; ack($m; $n-1))
  end;
ack(3; .);
def b_range_prop: [{ from: 1, upto: range(-.; .), by: range(-.; .) | select(. != 0) } | ([range(.from; .upto; .by)] | length) == ([(.upto - .from) / .by | ceil, 0] | max)] | map(select(.)) | length;
def b_cumsum: [foreach range(.) as $x (0; . + $x)];
def b_cumsum_xy: [foreach range(.) as $x (0; . + $x; $x, .)];
def b_str_slice: "a" * . | [range(length) as $x | .[$x:], .[:-$x]];
reduce range(rounds) as $r (0; (. * 31 + ([ (512 | b_upto), (2048 | b_reduce_update), (65536 | b_reverse), (65536 | b_sort),
      (65536 | b_group_by), (65536 | b_min_max), (8192 | b_add), (2048 | b_kv),
      (2048 | b_kv_update), (2048 | b_kv_entries), (1024 | b_ex_implode), (65536 | b_reduce),
      (65536 | b_try_catch), (1024 | b_repeat), (1024 | b_from), (65536 | b_last),
      (512 | b_pyramid), (12 | b_tree_contains), (12 | b_tree_flatten), (12 | b_tree_update),
      (12 | b_tree_paths), (4096 | b_to_fromjson), (5 | b_ack), (24 | b_range_prop),
      (65536 | b_cumsum), (65536 | b_cumsum_xy), (1024 | b_str_slice) ] | map(digest) | add) + $r) % 1000000007)
