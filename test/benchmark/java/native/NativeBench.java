import java.util.Arrays;
import java.util.function.Supplier;

/** Direct language ports. The harness only prepares, times, checks and reports. */
public final class NativeBench {
    static volatile Object observed;
    static void check(boolean ok) { if (!ok) throw new AssertionError("benchmark verification failed"); }
    static boolean warmup() { return !"0".equals(System.getenv("NATIVE_BENCH_WARMUP")); }
    static <T> void run(Supplier<T> work, java.util.function.Consumer<T> verify,
                        java.util.function.Consumer<T> report, boolean warm) {
        if (warm) { T value = work.get(); verify.accept(value); observed = value; }
        long started = System.nanoTime();
        T value = work.get();
        double elapsed = (System.nanoTime() - started) / 1e6;
        verify.accept(value); observed = value; report.accept(value);
        System.out.println("__TIMING__:" + elapsed);
    }
    static <S,T> void prepared(Supplier<S> prepare, java.util.function.Function<S,T> work,
                               java.util.function.Consumer<T> verify, java.util.function.Consumer<T> report) {
        if (warmup()) { T v=work.apply(prepare.get()); verify.accept(v); observed=v; }
        S state=prepare.get(); long start=System.nanoTime(); T value=work.apply(state);
        double elapsed=(System.nanoTime()-start)/1e6;
        verify.accept(value); observed=value; report.accept(value);
        System.out.println("__TIMING__:"+elapsed);
    }
    static void scalar(String name, Supplier<? extends Number> work, double expected) {
        run(work, value -> check(value.doubleValue() == expected),
            value -> System.out.println(name + ": PASS"), warmup());
    }
    static Benchmark awfy(String name) {
        return switch (name) {
            case "sieve" -> new Sieve(); case "permute" -> new Permute();
            case "queens" -> new Queens(); case "towers" -> new Towers();
            case "bounce" -> new Bounce(); case "list" -> new List();
            case "storage" -> new Storage(); case "mandelbrot" -> new Mandelbrot();
            case "richards" -> new Richards(); case "json" -> new Json();
            case "deltablue" -> new DeltaBlue(); case "havlak" -> new Havlak();
            case "cd" -> new CD();
            default -> throw new IllegalArgumentException("unknown AWFY benchmark " + name);
        };
    }
    static void runAwfy(String name, int inner, int outer) {
        String label = switch(name) { case "nbody" -> "NBody";case "deltablue" -> "DeltaBlue";
            case "cd" -> "CD";default -> Character.toUpperCase(name.charAt(0)) + name.substring(1); };
        // Upstream Java sources are compiled unchanged; counts come from Node wrappers.
        if (name.equals("nbody")) {
            run(() -> {
                for (int i=0;i<outer;i++) {
                    nbody.NBodySystem system = new nbody.NBodySystem();
                    for (int j=0;j<inner;j++) system.advance(0.01);
                    double expected = switch(inner) { case 1 -> -0.16907495402506745;
                        case 36000 -> -0.16901424478751628; case 250000 -> -0.1690859889909308;
                        default -> throw new IllegalArgumentException("unverified nbody count " + inner); };
                    check(system.energy() == expected);
                }
                return true;
            }, NativeBench::check, value -> System.out.println(label + ": PASS"), warmup());
            return;
        }
        run(() -> { Benchmark bench = awfy(name); for (int i=0;i<outer;i++) check(bench.innerBenchmarkLoop(inner)); return true; },
            NativeBench::check, value -> System.out.println(label + ": PASS"), warmup());
    }
    public static void main(String[] args) throws Exception {
        String[] entry = args[0].split("/", 2);
        switch (entry[0]) {
            case "r7rs" -> R7RS.run(entry[1]);
            case "julia" -> Micro.run(entry[1]);
            case "kostya" -> Kostya.run(entry[1]);
            case "beng" -> Beng.run(entry[1]);
            case "larceny" -> Larceny.run(entry[1]);
            case "jetstream" -> JetStream.run(entry[1], Integer.parseInt(args[1]));
            case "text" -> TextBench.run(entry[1]);
            case "awfy" -> runAwfy(entry[1], Integer.parseInt(args[1]), Integer.parseInt(args[2]));
            default -> throw new IllegalArgumentException("native port not implemented: " + args[0]);
        }
    }
}
