/* Native C2MIR port of the jq_mix text row: the jq-core VM (jq_core.h) runs
 * test/benchmark/text/jq/mix.jq, the same filter text as every column. */
#include "jq_core.h"

int main(void) {
    return jq_benchmark_main("jq_mix", JQ_INPUT_NULL, 0, 98172625LL);
}
