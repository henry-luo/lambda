// jq_bf, run by the jq bytecode VM written in typed Lambda
// (test/benchmark/jq_vm.ls), the counterpart of c2mir/jq_bf.c. The runner
// reports its time inside the C2MIR cell (vibe/impl/Lambda_Impl_Jq_Tests.md §1.2).
import ~~.jq_vm

pn main() {
    jq_vm_benchmark("jq_bf", JQ_INPUT_RAW, "test/benchmark/text/jq/fib.bf", 478890292)
}
