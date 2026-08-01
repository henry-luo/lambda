/* Native C2MIR port of beng/mandelbrot.ls. */
extern int printf(const char *, ...);
int main(void) {
    int checksum = 0; int byte_acc = 0; int bits = 0; int y;
    for (y = 0; y < 500; y++) { double ci = 2.0 * y / 500 - 1.0; int x; for (x = 0; x < 500; x++) { double zr = 0.0, zi = 0.0, zrzr = 0.0, zizi = 0.0, cr = 2.0 * x / 500 - 1.5; int iter = 0, escaped = 0; while (iter < 50 && !escaped) { double new_zr = zrzr - zizi + cr; zi = 2.0 * zr * zi + ci; zr = new_zr; zrzr = zr * zr; zizi = zi * zi; if (zrzr + zizi > 4.0) escaped = 1; iter++; } byte_acc = (byte_acc << 1) | !escaped; bits++; if (bits == 8) { checksum ^= byte_acc; byte_acc = 0; bits = 0; } else if (x == 499) { checksum ^= byte_acc << (8 - bits); byte_acc = 0; bits = 0; } } }
    printf("%d\n", checksum); return 0;
}
