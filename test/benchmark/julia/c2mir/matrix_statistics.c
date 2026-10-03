#include "micro_common.h"

int main(void) {
    const long long expected[4] = {464726438LL, 486656926LL, 47509838LL, 1966931148LL};
    return run_micro("matrix_statistics", matrix_statistics, expected);
}
