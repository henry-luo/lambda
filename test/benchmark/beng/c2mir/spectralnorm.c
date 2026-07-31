/* Native C2MIR port of beng/spectralnorm.ls. */
extern int printf(const char *, ...);
extern double sqrt(double);
static double eval_a(int i, int j) { int sum = i + j; return 1.0 / (sum * (sum + 1) / 2 + i + 1); }
static void multiply_a(int transpose, const double in[100], double out[100]) { int i; for (i = 0; i < 100; i++) { double sum = 0.0; int j; for (j = 0; j < 100; j++) sum += (transpose ? eval_a(j, i) : eval_a(i, j)) * in[j]; out[i] = sum; } }
static void multiply_ata(const double in[100], double out[100]) { double temp[100]; multiply_a(0, in, temp); multiply_a(1, temp, out); }
int main(void) {
    double u[100], v[100]; int i; double vbv = 0.0, vv = 0.0, result;
    for (i = 0; i < 100; i++) { u[i] = 1.0; v[i] = 0.0; }
    for (i = 0; i < 10; i++) { multiply_ata(u, v); multiply_ata(v, u); }
    for (i = 0; i < 100; i++) { vbv += u[i] * v[i]; vv += v[i] * v[i]; }
    result = sqrt(vbv / vv); printf("%.9f\n", result); return result < 1.274219990 || result > 1.274219992;
}
