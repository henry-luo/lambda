# jq_records: order-processing breadth workload over orders.json; the result
# is one integer checksum, so no float text or key order reaches the output.
def rounds: 15;
def total: reduce .items[] as $it (0; . + $it.qty * $it.price);
def safe_num(f): try (f | tonumber) catch 0;
def cents: . * 100 | floor;
def bump(f): f |= (. // 0) + 1;
def round_digest($r):
  ( map(select(.status != "pending")
        | .user.region //= "unknown"
        | .total = (total | cents)
        | .skus = ([.items[].sku] | unique)
        | del(.note)
        | with_entries(select(.value != null))) ) as $orders
  | ( $orders | group_by(.user.region)
      | map({ region: .[0].user.region, n: length, revenue: (map(.total) | add),
              top: (sort_by([0 - .total, .id]) | first | .id),
              paid: (map(select(.status == "paid")) | length) })
      | map(.n * 7 + .revenue + .top * 3 + .paid + (.region | length)) | add ) as $by_region
  | ( reduce ($orders[] | .items[]) as $it ({}; .[$it.sku] += $it.qty)
      | to_entries | sort_by([0 - .value, .key]) | .[:5]
      | from_entries | to_entries | map(.value + (.key | explode | add)) | add ) as $top_skus
  | ( [foreach $orders[] as $o (0; . + $o.total; . % 1000003)] | last ) as $running
  | ( $orders | map("\(.id),\(.user.name),\(.total)") | join("\n") | length ) as $csvlen
  | ( $orders[:200] | [paths(type == "number")] | length ) as $npaths
  | ( $orders[:200] | (.. | objects | select(has("qty")) | .qty) |= . * 2
      | [.. | numbers] | add | floor ) as $doubled
  | ( [ $orders[] | .user.tags | (.[0] // "-") + "/" + (.[1] // "-") ] | unique | length ) as $tagpairs
  | ( $orders | map(.user.name | ascii_upcase | split("R") | length) | add ) as $splits
  | ( [ $orders[:300][] | tojson ] | map(fromjson | .id) | add ) as $roundtrip
  | ( [ $orders[] | safe_num(.user.name[4:]) ] | add ) as $parsed
  | ( {} | bump(.a) | bump(.a) | bump(.b) | .a * 10 + .b ) as $counts
  | ($by_region + $top_skus + $running + $csvlen + $npaths + $doubled + $tagpairs
     + $splits + $roundtrip + $parsed + $counts + $r) % 1000000007;
. as $orders_in | reduce range(rounds) as $r (0; (. * 31 + ($orders_in | round_digest($r))) % 1000000007)
