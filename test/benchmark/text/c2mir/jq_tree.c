/* Native C2MIR port of the jq_tree text row: the jq-core VM (jq_core.h) runs
 * test/benchmark/text/jq/tree.jq, the same filter text as every column. */
#include "jq_core.h"

int main(void) {
    return jq_benchmark_main("jq_tree", JQ_INPUT_NULL, 0, 313746104LL);
}
