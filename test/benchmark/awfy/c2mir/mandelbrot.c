/* Native C2MIR port of awfy/mandelbrot2.ls. */
extern int printf(const char *, ...);

static int mandelbrot(void) {
    int sum = 0;
    int byte_acc = 0;
    int bit_num = 0;
    int y;
    for (y = 0; y < 500; y++) {
        double ci = 2.0 * y / 500 - 1.0;
        int x;
        for (x = 0; x < 500; x++) {
            double zrzr = 0.0;
            double zi = 0.0;
            double zizi = 0.0;
            double cr = 2.0 * x / 500 - 1.5;
            int z;
            int escape = 0;
            for (z = 0; z < 50; z++) {
                double zr = zrzr - zizi + cr;
                zi = 2.0 * zr * zi + ci;
                zrzr = zr * zr;
                zizi = zi * zi;
                if (zrzr + zizi > 4.0) { escape = 1; break; }
            }
            byte_acc = (byte_acc << 1) + escape;
            bit_num++;
            if (bit_num == 8) { sum ^= byte_acc; byte_acc = 0; bit_num = 0; }
            else if (x == 499) { sum ^= byte_acc << (8 - bit_num); byte_acc = 0; bit_num = 0; }
        }
    }
    return sum;
}

int main(void) {
    int result = mandelbrot();
    printf(result == 191 ? "Mandelbrot: PASS\n" : "Mandelbrot: FAIL result=%d\n", result);
    return result != 191;
}
