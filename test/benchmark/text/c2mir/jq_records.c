/* Native C2MIR port of the jq_records text row: the jq-core VM (jq_core.h) runs
 * test/benchmark/text/jq/records.jq, the same filter text as every column. */
#include "jq_core.h"

int main(void) {
    return jq_benchmark_main("jq_records", JQ_INPUT_JSON, "test/benchmark/text/jq/orders.json", 878885883LL);
}
