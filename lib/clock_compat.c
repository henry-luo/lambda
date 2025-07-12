#include <time.h>
#include <windows.h>

// Provide clock_gettime64 as a fallback to clock_gettime
// This is for compatibility with libraries that were compiled with newer MinGW
int clock_gettime64(int clk_id, struct _timespec64 *tp) {
    struct timespec ts;
    int result = clock_gettime(clk_id, &ts);
    if (result == 0) {
        tp->tv_sec = ts.tv_sec;
        tp->tv_nsec = ts.tv_nsec;
    }
    return result;
}
