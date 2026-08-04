// exercise AST binding identity beyond the removed loop-capture name limits.
const callbacks = [];
for (const this_is_a_lexical_for_head_name_that_is_longer_than_fifty_nine_chars of [1]) {
  for (const b of [2]) {
    for (const c of [3]) {
      for (const d of [4]) {
        for (const e of [5]) {
          for (const f of [6]) {
            for (const g of [7]) {
              for (const h of [8]) {
                for (const i of [9]) {
                  callbacks.push(() =>
                    this_is_a_lexical_for_head_name_that_is_longer_than_fifty_nine_chars +
                    b + c + d + e + f + g + h + i);
                }
              }
            }
          }
        }
      }
    }
  }
}
console.log(callbacks[0]());

const transitive = [];
for (const transitive_loop_capture_name_that_is_also_longer_than_fifty_nine_chars of [7]) {
  function make_callback() {
    return () => transitive_loop_capture_name_that_is_also_longer_than_fifty_nine_chars;
  }
  transitive.push(make_callback());
}
console.log(transitive[0]());
