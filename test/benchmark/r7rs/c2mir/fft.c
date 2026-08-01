/* Native C2MIR port of r7rs/fft2.ls. */
extern int printf(const char *, ...);
extern double sin(double);

static void four1(double data[4096], int n) {
    const double pi2 = 6.28318530717959;
    int i;
    int j = 0;
    int mmax;
    for (i = 0; i < n; i += 2) {
        int m;
        if (i < j) {
            double temp = data[i]; data[i] = data[j]; data[j] = temp;
            temp = data[i + 1]; data[i + 1] = data[j + 1]; data[j + 1] = temp;
        }
        m = n / 2;
        while (m >= 2 && j >= m) { j -= m; m /= 2; }
        j += m;
    }
    for (mmax = 2; mmax < n; mmax *= 2) {
        double theta = pi2 / mmax;
        double sin_half = sin(0.5 * theta);
        double wpr = -2.0 * sin_half * sin_half;
        double wpi = sin(theta);
        double wr = 1.0;
        double wi = 0.0;
        int m;
        for (m = 0; m < mmax; m += 2) {
            int ii;
            for (ii = m; ii < n; ii += 2 * mmax) {
                int jj = ii + mmax;
                double tempr = wr * data[jj] - wi * data[jj + 1];
                double tempi = wr * data[jj + 1] + wi * data[jj];
                data[jj] = data[ii] - tempr; data[jj + 1] = data[ii + 1] - tempi;
                data[ii] += tempr; data[ii + 1] += tempi;
            }
            {
                double new_wr = wr * wpr - wi * wpi + wr;
                wi = wi * wpr + wr * wpi + wi;
                wr = new_wr;
            }
        }
    }
}

int main(void) {
    double data[4096] = {0.0};
    four1(data, 4096);
    printf(data[0] == 0.0 ? "fft: PASS\n" : "fft: FAIL\n");
    return data[0] != 0.0;
}
