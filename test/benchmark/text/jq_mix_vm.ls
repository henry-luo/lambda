// jq_mix, run by the jq bytecode VM written in typed Lambda
// (test/benchmark/jq_vm.ls), the counterpart of c2mir/jq_mix.c. The runner
// reports its time inside the C2MIR cell (vibe/impl/Lambda_Impl_Jq_Tests.md §1.2).
import ~~.jq_vm

pn main() {
    jq_vm_benchmark("jq_mix", JQ_INPUT_NULL, "", 98172625)
}
