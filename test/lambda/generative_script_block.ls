// S16.7.7: script entry grants a bounded effect context to this block.
let outer = 40
function() {
    let started = clock()
    let printed = print("")
    let answer = outer + 2
    let samples = for (i in 1 to 3) uuid();
    let sample_count = len(samples) ^ { 0 };
    if (started is float and sample_count == 3 and
        samples[0] != samples[1] and samples[1] != samples[2]) answer else 0
}
