#include "micro_common.h"

int main(void) {
    const long long expected[4] = {1644834071848LL, 1644838824217LL, 206015869118LL, 5124750LL};
    return run_micro("iteration_pi_sum", iteration_pi_sum, expected);
}
