#include "micro_common.h"

int main(void) {
    const long long expected[4] = {592470661LL, 854479LL, 1966931148LL, 0LL};
    return run_micro("parse_integers", parse_integers, expected);
}
