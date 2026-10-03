#include "micro_common.h"

int main(void) {
    const long long expected[4] = {1177795LL, 584298900LL, 391LL, 100000LL};
    return run_micro("formatted_output", formatted_output, expected);
}
