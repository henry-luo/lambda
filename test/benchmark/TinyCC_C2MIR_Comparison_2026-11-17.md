# TinyCC vs C2MIR: C Benchmark Comparison

- **Date:** 2026-11-17
- **Platform:** macOS 26.5.2, arm64 (AArch64 Darwin)
- **Lambda repository commit:** `1323612342784e3d8240a2d8342df89e271fecff`
- **TinyCC:** 0.9.28rc, `mob@f830a16`; binary SHA-256 `f37bb4c91b007c33b69bd35b221497e5673ac9913127760149df6e73d671a06d`
- **C2MIR:** C2MIR driver built by `make c2mir-driver` from pinned MIR source `99c65079038f3ba9242ef646f308c266cfd7a8e5`; driver SHA-256 `41766c25d83dcc76dacfc17f26246eaa8f2113bc26f22ef900a3817d87719246`
- **Benchmark manifest:** 73 registered C ports; `run_c2mir_benchmarks.py` SHA-256 `fb28fa56d48e8dc1ae1cf10102c2d5c076c922425946130817d5507a3e4fe029`
- **C benchmark corpus:** 113 source/support files; SHA-256 `9996a848850dd3481c137c5468b1952f860f5dd9f7c7225ef7e36b9fbce03801`
- **Shared timer:** `bench_timer_main.c` SHA-256 `34dd062aabf2553904af9274e2cd61759226deb8e5412e974496d8df95122037`

## Result

C2MIR was faster overall on these C ports, both inside the timed workload and from compiler invocation through program exit. Ratios below are **C2MIR time divided by TinyCC time**; values below 1 favor C2MIR. Aggregate ratios are geometric means of the per-benchmark median ratios.

| Measure | Geometric mean ratio | Median per-benchmark ratio | C2MIR faster | TinyCC faster |
|---|---:|---:|---:|---:|
| Workload-only `__TIMING__` | 0.502× (about 1.99× faster) | 0.499× | 68/73 | 5/73 |
| End-to-end compile and run | 0.273× (about 3.66× faster) | 0.257× | 73/73 | 0/73 |

For workload time, C2MIR took about half of TinyCC’s time on the geometric mean. It was faster in 68 benchmarks; TinyCC was faster in 5, all in R7RS. For end-to-end time, C2MIR took about 27% of TinyCC’s time and was faster in all 73 benchmarks.

### Per-suite ratios

Each value is the geometric mean of that suite’s per-benchmark median ratios.

| Suite | Benchmarks | Workload C2MIR/TinyCC | End-to-end C2MIR/TinyCC | C2MIR workload wins |
|---|---:|---:|---:|---:|
| julia | 4 | 0.349× | 0.370× | 4/4 |
| r7rs | 10 | 0.675× | 0.195× | 5/10 |
| awfy | 14 | 0.486× | 0.243× | 14/14 |
| kostya | 7 | 0.537× | 0.275× | 7/7 |
| larceny | 12 | 0.440× | 0.249× | 12/12 |
| beng | 9 | 0.556× | 0.247× | 9/9 |
| jetstream | 6 | 0.468× | 0.276× | 6/6 |
| text | 11 | 0.484× | 0.456× | 11/11 |

## Per-benchmark medians

Cells show `median [minimum–maximum]` over the three measured samples, in milliseconds. Ratio columns use the medians. End-to-end includes the compiler process: TinyCC compile/link plus native program launch; C2MIR C parsing, MIR generation/JIT and execution.

| Benchmark | TinyCC workload | C2MIR workload | Work ratio | TinyCC end-to-end | C2MIR end-to-end | E2E ratio |
|---|---:|---:|---:|---:|---:|---:|
| `julia/parse_integers` | 7.635 [6.718–14.260] | 4.132 [3.829–7.456] | 0.541× | 462.602 [376.489–483.182] | 206.735 [170.411–207.909] | 0.447× |
| `julia/matrix_statistics` | 63.835 [61.443–81.761] | 13.125 [12.145–17.294] | 0.206× | 589.947 [370.375–896.167] | 187.877 [148.454–197.478] | 0.318× |
| `julia/iteration_pi_sum` | 20.728 [19.828–20.864] | 4.049 [3.842–4.238] | 0.195× | 436.174 [378.118–447.690] | 130.613 [124.920–207.446] | 0.299× |
| `julia/formatted_output` | 16.076 [12.698–19.039] | 10.933 [10.926–12.017] | 0.680× | 338.588 [329.969–383.639] | 149.162 [146.648–176.120] | 0.441× |
| `r7rs/ack` | 15.411 [15.157–16.502] | 15.615 [14.843–15.839] | 1.013× | 275.423 [258.449–466.747] | 74.977 [70.711–75.439] | 0.272× |
| `r7rs/cpstak` | 0.208 [0.207–0.212] | 0.315 [0.312–0.317] | 1.514× | 393.654 [293.439–438.108] | 58.983 [57.157–59.336] | 0.150× |
| `r7rs/fft` | 0.091 [0.089–0.194] | 0.037 [0.034–0.051] | 0.405× | 356.935 [326.047–465.362] | 65.900 [60.216–68.306] | 0.185× |
| `r7rs/fib` | 1.277 [1.208–1.892] | 1.620 [1.583–1.692] | 1.269× | 351.904 [336.207–459.140] | 67.732 [64.787–86.456] | 0.192× |
| `r7rs/fibfp` | 1.422 [1.418–2.371] | 1.933 [1.755–1.957] | 1.360× | 537.141 [424.363–616.541] | 81.261 [77.846–135.480] | 0.151× |
| `r7rs/mbrot` | 2.277 [2.274–2.330] | 0.774 [0.772–0.839] | 0.340× | 489.057 [373.592–503.039] | 77.923 [77.604–81.402] | 0.159× |
| `r7rs/nqueens` | 1.468 [0.979–1.495] | 0.524 [0.521–0.750] | 0.357× | 294.818 [270.806–340.264] | 75.837 [75.510–90.232] | 0.257× |
| `r7rs/sum` | 0.880 [0.812–2.728] | 0.421 [0.393–0.436] | 0.479× | 333.970 [326.624–362.931] | 81.240 [80.025–88.480] | 0.243× |
| `r7rs/sumfp` | 0.531 [0.400–0.531] | 0.122 [0.118–0.127] | 0.230× | 415.023 [328.278–492.136] | 100.096 [85.498–168.772] | 0.241× |
| `r7rs/tak` | 0.135 [0.134–0.177] | 0.185 [0.182–0.210] | 1.371× | 569.712 [459.047–671.077] | 86.595 [76.850–98.663] | 0.152× |
| `awfy/bounce` | 0.066 [0.066–0.089] | 0.040 [0.039–0.042] | 0.607× | 492.073 [383.383–526.971] | 83.297 [76.422–98.080] | 0.169× |
| `awfy/cd` | 45.012 [39.690–46.982] | 23.818 [23.636–27.849] | 0.529× | 365.048 [340.192–500.875] | 122.503 [115.675–130.013] | 0.336× |
| `awfy/deltablue` | 4.602 [4.565–4.661] | 2.151 [1.877–2.307] | 0.467× | 370.556 [323.970–518.239] | 98.684 [95.663–106.900] | 0.266× |
| `awfy/havlak` | 119.685 [113.576–154.761] | 67.206 [63.682–69.096] | 0.562× | 569.311 [393.641–611.608] | 150.852 [148.978–162.547] | 0.265× |
| `awfy/json` | 0.713 [0.709–0.715] | 0.353 [0.346–0.583] | 0.495× | 325.391 [302.450–348.125] | 76.431 [70.719–79.112] | 0.235× |
| `awfy/list` | 0.046 [0.045–0.047] | 0.032 [0.030–0.035] | 0.693× | 327.456 [290.637–372.951] | 62.559 [62.157–65.420] | 0.191× |
| `awfy/mandelbrot` | 146.745 [125.420–185.777] | 48.838 [43.281–56.402] | 0.333× | 492.880 [438.328–720.186] | 115.876 [110.017–134.937] | 0.235× |
| `awfy/nbody` | 6.657 [6.569–7.701] | 2.347 [2.329–4.258] | 0.353× | 429.914 [405.423–651.933] | 91.529 [89.141–99.367] | 0.213× |
| `awfy/permute` | 0.062 [0.061–0.062] | 0.044 [0.041–0.051] | 0.713× | 380.897 [325.850–427.517] | 88.466 [85.661–161.231] | 0.232× |
| `awfy/queens` | 0.051 [0.051–0.054] | 0.034 [0.033–0.040] | 0.665× | 345.240 [299.397–441.521] | 99.055 [89.552–100.827] | 0.287× |
| `awfy/richards` | 158.072 [115.953–166.288] | 46.176 [44.693–46.266] | 0.292× | 518.099 [477.602–691.970] | 132.167 [129.743–151.327] | 0.255× |
| `awfy/sieve` | 0.083 [0.075–0.303] | 0.025 [0.025–0.027] | 0.300× | 363.788 [348.861–510.711] | 104.198 [83.073–116.301] | 0.286× |
| `awfy/storage` | 0.271 [0.213–1.391] | 0.165 [0.157–0.198] | 0.609× | 333.805 [328.343–367.712] | 84.316 [79.229–92.151] | 0.253× |
| `awfy/towers` | 0.099 [0.093–0.101] | 0.047 [0.046–0.077] | 0.473× | 350.690 [308.410–373.510] | 80.391 [79.834–94.763] | 0.229× |
| `kostya/base64` | 1.460 [1.455–1.627] | 0.891 [0.886–0.989] | 0.610× | 436.721 [423.293–538.338] | 80.297 [80.033–89.337] | 0.184× |
| `kostya/brainfuck` | 67.533 [59.333–67.673] | 46.243 [45.417–48.819] | 0.685× | 440.157 [405.883–449.399] | 131.027 [129.677–138.581] | 0.298× |
| `kostya/collatz` | 524.338 [498.388–895.010] | 469.982 [466.992–570.547] | 0.896× | 1052.705 [891.477–2398.670] | 568.492 [566.402–666.556] | 0.540× |
| `kostya/levenshtein` | 3.399 [3.375–3.626] | 1.331 [1.191–2.451] | 0.392× | 392.146 [364.076–430.469] | 80.999 [79.842–101.219] | 0.207× |
| `kostya/json_gen` | 2.750 [2.499–3.084] | 2.542 [2.528–2.778] | 0.924× | 364.252 [325.263–383.413] | 96.709 [86.036–106.061] | 0.266× |
| `kostya/matmul` | 41.393 [35.091–44.948] | 11.388 [10.583–17.162] | 0.275× | 375.981 [365.054–438.893] | 96.313 [94.390–114.415] | 0.256× |
| `kostya/primes` | 7.704 [7.454–7.722] | 2.675 [2.378–3.299] | 0.347× | 326.034 [301.926–574.615] | 92.464 [86.310–95.560] | 0.284× |
| `larceny/array1` | 3.620 [3.549–3.624] | 0.514 [0.464–0.613] | 0.142× | 416.330 [402.233–455.703] | 86.664 [84.035–89.361] | 0.208× |
| `larceny/deriv` | 5.837 [5.762–5.842] | 4.649 [4.252–4.884] | 0.796× | 362.040 [288.351–363.771] | 79.866 [75.645–87.044] | 0.221× |
| `larceny/diviter` | 1313.331 [1222.441–1610.048] | 467.370 [390.434–496.878] | 0.356× | 1761.240 [1564.505–2001.545] | 566.258 [470.810–583.802] | 0.322× |
| `larceny/divrec` | 9.799 [9.100–9.861] | 7.010 [6.997–12.004] | 0.715× | 319.771 [281.634–381.401] | 74.535 [73.515–113.876] | 0.233× |
| `larceny/gcbench` | 258.731 [248.266–267.031] | 140.649 [139.083–146.759] | 0.544× | 689.887 [609.832–777.030] | 234.309 [229.947–238.402] | 0.340× |
| `larceny/pnpoly` | 7.497 [7.310–7.502] | 3.393 [3.388–3.515] | 0.453× | 443.034 [358.468–461.701] | 95.355 [90.117–97.603] | 0.215× |
| `larceny/paraffins` | 0.143 [0.133–0.208] | 0.081 [0.078–0.092] | 0.567× | 383.612 [335.413–566.148] | 82.894 [80.629–86.861] | 0.216× |
| `larceny/primes` | 8.284 [6.559–9.943] | 2.398 [2.370–2.432] | 0.289× | 461.278 [364.239–506.057] | 88.222 [85.758–99.783] | 0.191× |
| `larceny/puzzle` | 2.890 [2.776–2.903] | 1.831 [1.722–1.873] | 0.634× | 320.853 [291.509–335.297] | 82.509 [73.980–87.529] | 0.257× |
| `larceny/quicksort` | 0.793 [0.789–0.846] | 0.323 [0.322–0.323] | 0.407× | 303.596 [285.649–342.782] | 69.896 [68.938–75.353] | 0.230× |
| `larceny/ray` | 0.610 [0.609–0.626] | 0.243 [0.243–0.262] | 0.398× | 271.645 [271.618–309.168] | 72.135 [67.732–72.684] | 0.266× |
| `larceny/triangl` | 202.153 [193.354–214.606] | 88.080 [84.980–92.803] | 0.436× | 440.088 [432.125–487.596] | 154.441 [154.022–169.617] | 0.351× |
| `beng/binarytrees` | 7.559 [7.362–8.659] | 4.295 [4.285–4.387] | 0.568× | 328.209 [299.002–381.623] | 69.916 [69.517–72.702] | 0.213× |
| `beng/fannkuch` | 0.466 [0.425–0.473] | 0.216 [0.213–0.242] | 0.463× | 338.071 [326.542–350.931] | 90.230 [86.742–97.661] | 0.267× |
| `beng/fasta` | 0.498 [0.480–0.499] | 0.423 [0.420–0.427] | 0.850× | 339.157 [328.171–352.277] | 88.781 [87.943–110.872] | 0.262× |
| `beng/knucleotide` | 0.702 [0.692–0.705] | 0.503 [0.502–0.533] | 0.716× | 304.599 [264.632–307.795] | 85.436 [83.586–90.008] | 0.280× |
| `beng/mandelbrot` | 74.414 [74.180–75.871] | 23.953 [22.944–24.552] | 0.322× | 430.049 [423.614–498.189] | 95.797 [94.685–103.897] | 0.223× |
| `beng/pidigits` | 0.119 [0.112–0.185] | 0.070 [0.068–0.081] | 0.589× | 311.976 [247.111–322.262] | 71.192 [68.617–76.833] | 0.228× |
| `beng/regexredux` | 2.297 [2.168–2.343] | 1.653 [1.534–1.869] | 0.720× | 277.075 [220.783–351.099] | 69.141 [66.890–72.613] | 0.250× |
| `beng/revcomp` | 0.649 [0.605–0.767] | 0.541 [0.541–0.548] | 0.833× | 268.261 [256.702–283.260] | 68.780 [66.228–68.855] | 0.256× |
| `beng/spectralnorm` | 1.813 [1.692–1.848] | 0.505 [0.473–0.506] | 0.279× | 283.892 [241.711–353.487] | 70.772 [67.187–76.470] | 0.249× |
| `jetstream/crypto_sha1` | 5.842 [5.768–5.843] | 3.811 [3.786–3.825] | 0.652× | 293.506 [280.003–303.211] | 72.929 [72.740–77.080] | 0.248× |
| `jetstream/cube3d` | 1.833 [1.704–2.340] | 0.671 [0.660–0.672] | 0.366× | 303.148 [267.515–373.516] | 74.804 [73.579–91.363] | 0.247× |
| `jetstream/hashmap` | 9.376 [7.774–10.829] | 4.516 [4.394–5.377] | 0.482× | 280.649 [278.056–310.562] | 82.379 [80.388–82.679] | 0.294× |
| `jetstream/navier_stokes` | 155.064 [136.627–166.619] | 77.451 [74.995–99.243] | 0.499× | 513.483 [512.088–542.615] | 162.214 [162.138–187.777] | 0.316× |
| `jetstream/raytrace3d` | 10.650 [9.478–11.872] | 3.820 [3.751–4.490] | 0.359× | 417.854 [401.137–431.705] | 101.603 [97.263–107.558] | 0.243× |
| `jetstream/splay` | 71.626 [70.273–74.475] | 36.387 [33.068–42.453] | 0.508× | 402.863 [367.923–425.417] | 127.586 [118.275–134.726] | 0.317× |
| `text/fast_diff` | 65.877 [65.537–81.031] | 22.361 [19.753–23.959] | 0.339× | 388.161 [357.145–401.617] | 100.160 [96.290–107.839] | 0.258× |
| `text/microdiff` | 10.503 [8.919–11.072] | 4.865 [4.636–4.958] | 0.463× | 315.399 [278.411–368.069] | 125.840 [99.087–168.073] | 0.399× |
| `text/hyphen` | 6.336 [5.984–8.859] | 2.556 [2.105–2.563] | 0.403× | 276.590 [268.485–302.366] | 132.494 [126.121–142.787] | 0.479× |
| `text/prettier_ast` | 137.050 [134.775–154.872] | 67.272 [60.653–68.097] | 0.491× | 407.491 [375.775–536.678] | 166.234 [161.503–172.549] | 0.408× |
| `text/text_search` | 1879.410 [1866.615–1941.411] | 720.092 [717.898–736.657] | 0.383× | 2137.576 [2122.486–2247.736] | 790.061 [783.670–811.717] | 0.370× |
| `text/three_way_merge` | 5145.090 [5137.468–5480.301] | 3104.214 [3099.002–3136.255] | 0.603× | 5460.000 [5426.621–5802.498] | 3200.118 [3168.396–3210.809] | 0.586× |
| `text/log_pipeline` | 1717.907 [1656.466–1721.370] | 947.853 [926.740–1016.672] | 0.552× | 2013.512 [1921.282–2018.065] | 1029.337 [1002.177–1093.022] | 0.511× |
| `text/jq_mix` | 2943.959 [2857.487–2970.274] | 1452.618 [1445.536–1551.004] | 0.493× | 3214.647 [3094.157–3374.840] | 1590.824 [1590.429–1694.814] | 0.495× |
| `text/jq_records` | 2536.900 [2519.734–2598.055] | 1450.046 [1417.672–1482.284] | 0.572× | 2974.168 [2810.955–3034.729] | 1592.982 [1569.514–1625.464] | 0.536× |
| `text/jq_bf` | 5185.909 [5062.342–5258.872] | 2904.249 [2898.523–2919.423] | 0.560× | 5515.419 [5496.554–5587.021] | 3048.941 [3040.135–3064.449] | 0.553× |
| `text/jq_tree` | 16972.284 [14784.442–21591.762] | 9142.857 [7479.300–10487.969] | 0.539× | 17239.502 [15087.487–22148.026] | 9315.853 [7614.090–10671.908] | 0.540× |

## Method

- The 73 rows are the registered workloads in `test/benchmark/run_c2mir_benchmarks.py`: Julia 4, R7RS 10, AWFY 14, Kostya 7, Larceny 12, Beng 9, JetStream 6 and Text 11.
- Each engine received one warmup per benchmark, followed by three alternating measured pairs. Pair order alternated between TinyCC-first and C2MIR-first. Each invocation had a 180-second timeout.
- TinyCC compiled every sample with `tcc -B./ref/tinycc -O2 -Dmain=c2mir_bench_body <source> bench_timer_main.c -o <binary>`, then launched the generated native executable. C2MIR used the benchmark runner’s `c2m -Dmain=c2mir_bench_body <source> bench_timer_main.c -eg` invocation. The four Julia microbenchmarks compile directly without the shared timer main and use their source-defined timer.
- Workload time is the port’s `__TIMING__` region, excluding compiler/JIT startup. The other 69 ports use the shared `bench_timer_main.c` around the same C `main` body. Julia microbenchmarks use their in-source timer.
- End-to-end starts immediately before invoking the compiler and stops after the program exits. TinyCC includes compile/link and launch; C2MIR includes frontend parsing, MIR generation/JIT and execution. These are fresh compiler invocations after a warmup, not cold-cache measurements.
- The compilers use their native settings as shown above (`tcc -O2` and the repository C2MIR runner’s `-eg` path). This is a practical toolchain comparison, not a claim that their optimization levels are equivalent.
- All benchmark outputs were identical between TinyCC and C2MIR for all 73 rows after removing the timing marker. Each run exited successfully and emitted a workload timing.

## Correctness notes

Both compilers matched the registered exact expected-output strings in 71/73 rows. `awfy/cd` and `awfy/havlak` are the two exceptions: both compilers print the same trailing computed result (`4305` and `160505213`, respectively), while the expected strings in `run_c2mir_benchmarks.py` stop at the `PASS` line. Their own checks pass, exit status is zero, and TinyCC/C2MIR outputs match byte for byte. The expected strings and sources were not changed for this comparison.

## Interpretation and limits

TinyCC compiled and ran every registered benchmark, with output identical to C2MIR. For this suite and these compiler settings, the C2MIR JIT produced faster timed workloads on most rows. TinyCC’s compile time alone had a median of 44.6 ms across the 73 per-benchmark medians, but the complete compile-and-launch measurement still favored C2MIR on every row.

Three measured pairs give a suite-level comparison, not a stable ranking for every sub-millisecond row. The per-row ranges are included to show timing spread; use additional paired samples for decisions about individual near-tie workloads.
