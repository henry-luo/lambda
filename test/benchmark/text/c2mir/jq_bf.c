/* Native C2MIR port of the jq_bf text row: the jq-core VM (jq_core.h) runs
 * test/benchmark/text/jq/bf.jq, the same filter text as every column. */
#include "jq_core.h"

int main(void) {
    return jq_benchmark_main("jq_bf", JQ_INPUT_RAW, "test/benchmark/text/jq/fib.bf", 478890292LL);
}
