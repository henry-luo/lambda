# Transpiling Time Profile Report

Scripts profiled: 278 (223 standalone, 55 with imports, 1 skipped)


---

## Set 1: Standalone Scripts (no imports) — 223 scripts

These timings reflect a single module and are the most direct measure of each compilation phase.


### Phase Summary (standalone)

| Phase | Total (ms) | Avg (ms) | % of Total |
|-------|-----------|----------|------------|
| Tree-sitter Parse | 64.61 | 0.29 | 5.6% |
| AST Build | 243.08 | 1.09 | 20.9% |
| MIR Transpile | 798.83 | 3.58 | 68.8% |
| JIT Codegen | 53.90 | 0.24 | 4.6% |
| **Total** | **1160.40** | **5.20** | 100% |

| Suite | Scripts | Parse (ms) | AST (ms) | Transpile (ms) | JIT (ms) | Total (ms) | Avg (ms) |
|-------|---------|-----------|----------|---------------|----------|-----------|----------|
| awfy | 24 | 15.56 | 18.78 | 163.22 | 7.63 | 205.19 | 8.55 |
| beng | 7 | 2.46 | 2.14 | 23.85 | 1.52 | 29.98 | 4.28 |
| chart | 1 | 0.12 | 132.60 | 1.02 | 0.10 | 133.85 | 133.85 |
| kostya | 7 | 1.56 | 1.66 | 16.71 | 1.44 | 21.38 | 3.05 |
| lambda | 116 | 30.30 | 45.17 | 453.77 | 28.56 | 557.79 | 4.81 |
| larceny | 14 | 3.79 | 3.58 | 33.03 | 2.91 | 43.31 | 3.09 |
| latex | 1 | 0.24 | 24.91 | 3.39 | 0.13 | 28.67 | 28.67 |
| math | 1 | 0.37 | 2.99 | 4.20 | 0.14 | 7.70 | 7.70 |
| proc | 32 | 7.07 | 7.56 | 66.55 | 7.61 | 88.79 | 2.77 |
| r7rs | 20 | 3.14 | 3.69 | 33.08 | 3.84 | 43.75 | 2.19 |

### Per-Script Breakdown (standalone)

| # | Suite | Script | Parse (ms) | AST (ms) | Transpile (ms) | JIT (ms) | Total (ms) |
|---|-------|--------|-----------|----------|---------------|----------|-----------|
| 1 | chart | chart_test_repeat_chart | 0.12 | 132.60 | 1.02 | 0.10 | 133.85 |
| 2 | lambda | complex_iot_report_html | 2.31 | 3.61 | 49.21 | 0.65 | 55.78 |
| 3 | awfy | awfy_cd | 2.14 | 3.15 | 32.45 | 0.79 | 38.53 |
| 4 | lambda | type_pattern | 1.51 | 2.18 | 25.16 | 0.46 | 29.31 |
| 5 | latex | latex_test_latex_picture | 0.24 | 24.91 | 3.39 | 0.13 | 28.67 |
| 6 | awfy | awfy_deltablue2 | 2.10 | 2.57 | 19.93 | 0.79 | 25.40 |
| 7 | awfy | awfy_deltablue | 1.93 | 2.25 | 17.90 | 0.57 | 22.65 |
| 8 | awfy | awfy_havlak2 | 1.94 | 2.46 | 15.41 | 0.59 | 20.40 |
| 9 | awfy | awfy_havlak | 1.67 | 2.26 | 15.17 | 0.58 | 19.67 |
| 10 | lambda | vector_sys_func | 0.65 | 0.97 | 12.97 | 0.34 | 14.94 |
| 11 | lambda | expr_stam | 0.67 | 0.65 | 12.74 | 0.37 | 14.44 |
| 12 | lambda | pipe_where | 0.26 | 0.30 | 12.41 | 0.34 | 13.30 |
| 13 | lambda | for_clauses_test | 0.49 | 0.64 | 11.12 | 0.30 | 12.55 |
| 14 | lambda | numeric_sys_func | 0.40 | 0.58 | 11.21 | 0.33 | 12.52 |
| 15 | proc | proc_proc_fill | 0.57 | 0.64 | 10.47 | 0.42 | 12.10 |
| 16 | awfy | awfy_nbody | 0.39 | 0.44 | 10.76 | 0.30 | 11.89 |
| 17 | awfy | awfy_richards2 | 1.12 | 1.11 | 8.93 | 0.36 | 11.52 |
| 18 | lambda | comp_expr | 0.47 | 0.56 | 9.84 | 0.33 | 11.19 |
| 19 | lambda | array_float | 1.80 | 2.91 | 4.21 | 1.66 | 10.58 |
| 20 | awfy | awfy_richards | 0.90 | 0.96 | 8.33 | 0.34 | 10.54 |
| 21 | lambda | numeric_expr | 0.34 | 0.44 | 8.62 | 0.38 | 9.78 |
| 22 | lambda | comp_expr_edge | 0.32 | 0.34 | 7.96 | 0.30 | 8.92 |
| 23 | lambda | decimal | 0.25 | 0.67 | 7.40 | 0.28 | 8.59 |
| 24 | beng | beng_nbody | 0.55 | 0.53 | 7.04 | 0.27 | 8.38 |
| 25 | awfy | awfy_nbody2 | 0.42 | 0.46 | 7.00 | 0.25 | 8.13 |
| 26 | lambda | type_occurrence | 0.41 | 0.59 | 6.68 | 0.25 | 7.93 |
| 27 | lambda | expr | 0.39 | 0.45 | 6.63 | 0.27 | 7.74 |
| 28 | math | math_test_math_util | 0.37 | 2.99 | 4.20 | 0.14 | 7.70 |
| 29 | lambda | error_propagation | 0.69 | 0.59 | 5.88 | 0.37 | 7.52 |
| 30 | lambda | string_pattern | 0.35 | 0.47 | 6.33 | 0.27 | 7.42 |
| 31 | lambda | vector_performance | 0.37 | 0.51 | 6.24 | 0.27 | 7.38 |
| 32 | r7rs | r7rs_sum2 | 0.14 | 0.28 | 6.66 | 0.21 | 7.30 |
| 33 | lambda | box_unbox_advanced | 0.85 | 0.55 | 5.38 | 0.31 | 7.09 |
| 34 | lambda | pipe_spread | 0.17 | 0.20 | 6.41 | 0.28 | 7.06 |
| 35 | lambda | vector_advanced | 0.35 | 0.49 | 5.69 | 0.25 | 6.78 |
| 36 | lambda | string_funcs | 0.26 | 0.34 | 5.77 | 0.23 | 6.59 |
| 37 | larceny | larceny_paraffins | 0.40 | 0.42 | 5.18 | 0.23 | 6.23 |
| 38 | lambda | for_decompose | 0.26 | 0.35 | 5.25 | 0.24 | 6.10 |
| 39 | lambda | match_expr | 0.32 | 0.35 | 5.16 | 0.27 | 6.10 |
| 40 | lambda | datetime | 0.41 | 0.40 | 4.88 | 0.23 | 5.92 |
| 41 | lambda | path | 0.29 | 0.34 | 5.04 | 0.24 | 5.90 |
| 42 | beng | beng_knucleotide | 0.39 | 0.38 | 4.88 | 0.24 | 5.90 |
| 43 | lambda | func_param2 | 0.32 | 0.44 | 4.83 | 0.25 | 5.84 |
| 44 | lambda | sys_func_native_cmp | 0.35 | 0.43 | 4.77 | 0.28 | 5.83 |
| 45 | lambda | constrained_type | 0.20 | 0.23 | 5.09 | 0.27 | 5.79 |
| 46 | larceny | larceny_triangl | 0.61 | 0.48 | 4.44 | 0.23 | 5.76 |
| 47 | lambda | import | 0.08 | 4.80 | 0.73 | 0.10 | 5.72 |
| 48 | lambda | simple_expr | 0.34 | 0.39 | 4.72 | 0.25 | 5.70 |
| 49 | lambda | vector_basic | 0.25 | 0.40 | 4.77 | 0.24 | 5.66 |
| 50 | lambda | structural_equality | 0.25 | 0.27 | 4.82 | 0.25 | 5.59 |
| 51 | lambda | test_string_pattern_integration | 0.31 | 0.37 | 4.32 | 0.24 | 5.24 |
| 52 | larceny | larceny_deriv2 | 0.60 | 0.36 | 3.99 | 0.25 | 5.20 |
| 53 | kostya | kostya_base64 | 0.27 | 0.32 | 4.21 | 0.23 | 5.04 |
| 54 | lambda | string_indexable | 0.20 | 0.25 | 4.24 | 0.23 | 4.93 |
| 55 | awfy | awfy_bounce2 | 0.25 | 0.27 | 4.17 | 0.23 | 4.92 |
| 56 | lambda | namespace | 0.26 | 0.32 | 4.08 | 0.22 | 4.87 |
| 57 | lambda | value | 0.20 | 0.24 | 4.11 | 0.23 | 4.79 |
| 58 | proc | proc_proc_typed_array_param | 0.44 | 0.34 | 3.65 | 0.29 | 4.73 |
| 59 | awfy | awfy_bounce | 0.23 | 0.24 | 4.00 | 0.22 | 4.69 |
| 60 | lambda | func | 0.22 | 0.28 | 3.84 | 0.26 | 4.59 |
| 61 | lambda | int64 | 0.27 | 0.32 | 3.78 | 0.22 | 4.58 |
| 62 | lambda | that_implicit_name | 0.15 | 0.17 | 4.02 | 0.24 | 4.58 |
| 63 | lambda | transpile_typed_closure | 0.23 | 0.30 | 3.67 | 0.26 | 4.47 |
| 64 | lambda | pipe_type_infer | 0.15 | 0.20 | 3.79 | 0.24 | 4.39 |
| 65 | beng | beng_fannkuch | 0.60 | 0.26 | 3.26 | 0.22 | 4.34 |
| 66 | lambda | closure_advanced | 0.25 | 0.35 | 3.43 | 0.28 | 4.31 |
| 67 | r7rs | r7rs_nqueens2 | 0.29 | 0.36 | 3.41 | 0.23 | 4.29 |
| 68 | proc | proc_proc_array_type_convert | 0.24 | 0.29 | 3.51 | 0.25 | 4.29 |
| 69 | lambda | vmap | 0.21 | 0.29 | 3.56 | 0.22 | 4.27 |
| 70 | lambda | for_element_spread | 0.17 | 0.23 | 3.56 | 0.22 | 4.18 |
| 71 | lambda | let_for_array | 0.38 | 0.31 | 3.18 | 0.23 | 4.09 |
| 72 | lambda | transpile_bitwise | 0.26 | 0.31 | 3.23 | 0.24 | 4.04 |
| 73 | lambda | type2 | 0.16 | 0.18 | 3.35 | 0.21 | 3.90 |
| 74 | lambda | tail_call | 0.29 | 0.25 | 3.10 | 0.24 | 3.88 |
| 75 | r7rs | r7rs_fft | 0.22 | 0.25 | 3.09 | 0.20 | 3.77 |
| 76 | lambda | type_negation | 0.17 | 0.22 | 3.15 | 0.21 | 3.76 |
| 77 | lambda | decimal_big | 0.14 | 0.18 | 3.14 | 0.23 | 3.70 |
| 78 | lambda | transpile_error_ret_types | 0.27 | 0.32 | 2.76 | 0.28 | 3.64 |
| 79 | lambda | sys_func_native_math | 0.25 | 0.30 | 2.80 | 0.25 | 3.60 |
| 80 | lambda | large_int_map | 0.21 | 0.23 | 2.92 | 0.22 | 3.58 |
| 81 | lambda | is_in_precedence | 0.24 | 0.34 | 2.77 | 0.22 | 3.58 |
| 82 | lambda | for_element_filter | 0.15 | 0.19 | 3.04 | 0.20 | 3.58 |
| 83 | lambda | child_query | 0.51 | 0.29 | 2.54 | 0.20 | 3.54 |
| 84 | r7rs | r7rs_nqueens | 0.26 | 0.27 | 2.77 | 0.22 | 3.53 |
| 85 | proc | proc_proc_element_mutation | 0.30 | 0.26 | 2.73 | 0.23 | 3.53 |
| 86 | kostya | kostya_brainfuck | 0.23 | 0.24 | 2.83 | 0.21 | 3.51 |
| 87 | lambda | sys_func_is_nan | 0.15 | 0.19 | 2.92 | 0.21 | 3.48 |
| 88 | r7rs | r7rs_fft2 | 0.27 | 0.28 | 2.70 | 0.22 | 3.48 |
| 89 | proc | proc_proc_var | 0.35 | 0.40 | 2.45 | 0.23 | 3.43 |
| 90 | kostya | kostya_levenshtein | 0.25 | 0.27 | 2.69 | 0.21 | 3.43 |
| 91 | lambda | null_safe_member | 0.22 | 0.29 | 2.71 | 0.21 | 3.42 |
| 92 | lambda | match_string_pattern | 0.14 | 0.18 | 2.90 | 0.19 | 3.41 |
| 93 | kostya | kostya_json_gen | 0.29 | 0.27 | 2.62 | 0.21 | 3.39 |
| 94 | larceny | larceny_pnpoly | 0.23 | 0.28 | 2.65 | 0.21 | 3.36 |
| 95 | beng | beng_spectralnorm | 0.25 | 0.28 | 2.60 | 0.21 | 3.34 |
| 96 | beng | beng_regexredux | 0.28 | 0.27 | 2.56 | 0.20 | 3.31 |
| 97 | lambda | unboxed_field_access | 0.21 | 0.23 | 2.62 | 0.22 | 3.29 |
| 98 | proc | proc_test_pipe_file | 0.23 | 0.31 | 2.48 | 0.25 | 3.27 |
| 99 | larceny | larceny_ray | 0.29 | 0.32 | 2.42 | 0.20 | 3.23 |
| 100 | lambda | comment | 0.18 | 0.23 | 2.60 | 0.21 | 3.23 |
| 101 | proc | proc_proc_param_type_infer | 0.28 | 0.30 | 2.38 | 0.21 | 3.18 |
| 102 | lambda | transpile_idiv_mod | 0.19 | 0.26 | 2.48 | 0.21 | 3.15 |
| 103 | lambda | unboxed_sys_func | 0.19 | 0.24 | 2.48 | 0.24 | 3.15 |
| 104 | proc | proc_clock | 0.33 | 0.18 | 1.64 | 0.99 | 3.14 |
| 105 | proc | proc_proc_array_set | 0.17 | 0.20 | 2.53 | 0.23 | 3.13 |
| 106 | lambda | sys_func_math_extended | 0.25 | 0.27 | 2.38 | 0.22 | 3.13 |
| 107 | proc | proc_proc_map_type_change | 0.27 | 0.28 | 2.25 | 0.21 | 3.01 |
| 108 | proc | proc_vmap | 0.25 | 0.27 | 2.27 | 0.22 | 3.01 |
| 109 | awfy | awfy_queens | 0.21 | 0.23 | 2.33 | 0.22 | 2.99 |
| 110 | kostya | kostya_matmul | 0.23 | 0.23 | 2.24 | 0.22 | 2.92 |
| 111 | lambda | in_container | 0.12 | 0.19 | 2.39 | 0.21 | 2.90 |
| 112 | larceny | larceny_deriv | 0.27 | 0.27 | 2.14 | 0.21 | 2.89 |
| 113 | lambda | string_prefix_suffix | 0.14 | 0.18 | 2.37 | 0.21 | 2.89 |
| 114 | lambda | query | 0.18 | 0.23 | 2.27 | 0.19 | 2.87 |
| 115 | proc | proc_proc_closure_mutation | 0.23 | 0.26 | 2.12 | 0.22 | 2.83 |
| 116 | proc | proc_while_swap | 0.33 | 0.36 | 1.92 | 0.21 | 2.83 |
| 117 | proc | proc_proc_arr_concat | 0.18 | 0.26 | 2.19 | 0.20 | 2.82 |
| 118 | lambda | closure | 0.18 | 0.26 | 2.13 | 0.22 | 2.79 |
| 119 | larceny | larceny_gcbench | 0.22 | 0.22 | 2.13 | 0.21 | 2.78 |
| 120 | larceny | larceny_quicksort | 0.20 | 0.22 | 2.14 | 0.21 | 2.77 |
| 121 | awfy | awfy_towers2 | 0.25 | 0.25 | 2.04 | 0.22 | 2.76 |
| 122 | awfy | awfy_queens2 | 0.22 | 0.23 | 2.09 | 0.22 | 2.75 |
| 123 | proc | proc_proc_semicolon | 0.26 | 0.31 | 1.92 | 0.25 | 2.74 |
| 124 | lambda | nested_shadowing | 0.16 | 0.19 | 2.15 | 0.22 | 2.73 |
| 125 | lambda | method_call | 0.16 | 0.23 | 2.07 | 0.26 | 2.72 |
| 126 | lambda | transpile_len_typed | 0.19 | 0.25 | 2.03 | 0.21 | 2.68 |
| 127 | lambda | string_pattern_ops | 0.15 | 0.20 | 2.12 | 0.20 | 2.67 |
| 128 | proc | proc_proc_else_if | 0.27 | 0.26 | 1.91 | 0.21 | 2.65 |
| 129 | r7rs | r7rs_mbrot2 | 0.23 | 0.24 | 1.92 | 0.20 | 2.58 |
| 130 | beng | beng_binarytrees | 0.19 | 0.20 | 1.97 | 0.20 | 2.56 |
| 131 | lambda | empty_string_null | 0.16 | 0.17 | 2.01 | 0.21 | 2.55 |
| 132 | lambda | float_conversion | 0.15 | 0.19 | 1.96 | 0.19 | 2.49 |
| 133 | proc | proc_tail_call_proc | 0.19 | 0.21 | 1.86 | 0.21 | 2.48 |
| 134 | lambda | pipe_sysfunc | 0.12 | 0.16 | 1.94 | 0.20 | 2.42 |
| 135 | larceny | larceny_puzzle | 0.18 | 0.19 | 1.78 | 0.26 | 2.41 |
| 136 | larceny | larceny_gcbench2 | 0.20 | 0.20 | 1.79 | 0.20 | 2.40 |
| 137 | lambda | typed_map_direct_access | 0.16 | 0.17 | 1.86 | 0.20 | 2.39 |
| 138 | lambda | error_handling | 0.19 | 0.22 | 1.74 | 0.22 | 2.37 |
| 139 | awfy | awfy_towers | 0.21 | 0.23 | 1.71 | 0.21 | 2.36 |
| 140 | lambda | spread | 0.14 | 0.20 | 1.81 | 0.19 | 2.35 |
| 141 | lambda | string | 0.11 | 0.15 | 1.87 | 0.20 | 2.33 |
| 142 | lambda | string_ord_chr | 0.11 | 0.15 | 1.87 | 0.18 | 2.32 |
| 143 | lambda | mixed_numeric_ops | 0.25 | 0.16 | 1.70 | 0.20 | 2.31 |
| 144 | proc | proc_proc_dir_listing | 0.20 | 0.17 | 1.75 | 0.18 | 2.30 |
| 145 | proc | proc_proc_control | 0.23 | 0.26 | 1.61 | 0.19 | 2.30 |
| 146 | lambda | first_class_fn | 0.15 | 0.17 | 1.73 | 0.20 | 2.25 |
| 147 | lambda | box_unbox_negative | 0.15 | 0.19 | 1.68 | 0.21 | 2.23 |
| 148 | proc | proc_proc_bitwise_int64 | 0.18 | 0.20 | 1.66 | 0.18 | 2.22 |
| 149 | lambda | sys_func_math_constants | 0.18 | 0.20 | 1.63 | 0.19 | 2.21 |
| 150 | lambda | namespace_v2 | 0.17 | 0.20 | 1.64 | 0.19 | 2.20 |
| 151 | r7rs | r7rs_mbrot | 0.19 | 0.22 | 1.58 | 0.19 | 2.19 |
| 152 | proc | proc_proc_param_mutation | 0.16 | 0.18 | 1.64 | 0.19 | 2.18 |
| 153 | lambda | chained_comparisons | 0.12 | 0.11 | 1.74 | 0.20 | 2.17 |
| 154 | beng | beng_mandelbrot | 0.19 | 0.22 | 1.54 | 0.19 | 2.15 |
| 155 | proc | proc_proc_div_mod | 0.16 | 0.18 | 1.61 | 0.19 | 2.14 |
| 156 | lambda | trim | 0.12 | 0.18 | 1.60 | 0.18 | 2.08 |
| 157 | lambda | correlation_math | 0.15 | 0.17 | 1.56 | 0.20 | 2.08 |
| 158 | lambda | split_null_concat | 0.11 | 0.16 | 1.61 | 0.19 | 2.06 |
| 159 | lambda | box_unbox | 0.12 | 0.35 | 1.36 | 0.21 | 2.04 |
| 160 | lambda | typed_param_string | 0.12 | 0.15 | 1.53 | 0.19 | 1.99 |
| 161 | awfy | awfy_storage2 | 0.19 | 0.19 | 1.38 | 0.21 | 1.97 |
| 162 | awfy | awfy_mandelbrot2 | 0.22 | 0.24 | 1.32 | 0.20 | 1.97 |
| 163 | awfy | awfy_permute2 | 0.18 | 0.20 | 1.39 | 0.19 | 1.97 |
| 164 | lambda | import_arrays | 0.08 | 1.30 | 0.46 | 0.11 | 1.94 |
| 165 | lambda | forward_ref | 0.12 | 0.15 | 1.44 | 0.23 | 1.94 |
| 166 | awfy | awfy_storage | 0.16 | 0.17 | 1.37 | 0.20 | 1.90 |
| 167 | proc | proc_proc_cmd | 0.14 | 0.16 | 1.40 | 0.19 | 1.89 |
| 168 | lambda | map_spread_override | 0.16 | 0.19 | 1.35 | 0.19 | 1.89 |
| 169 | lambda | func_param | 0.14 | 0.18 | 1.33 | 0.20 | 1.84 |
| 170 | lambda | builtin_import_alias | 0.11 | 0.16 | 1.17 | 0.39 | 1.82 |
| 171 | lambda | math_random | 0.12 | 0.17 | 1.34 | 0.19 | 1.81 |
| 172 | lambda | robust_coverage | 0.13 | 0.15 | 1.33 | 0.20 | 1.81 |
| 173 | lambda | if_expr_types | 0.14 | 0.15 | 1.32 | 0.19 | 1.79 |
| 174 | awfy | awfy_list | 0.17 | 0.19 | 1.21 | 0.20 | 1.76 |
| 175 | proc | proc_proc_var_type_widen | 0.16 | 0.21 | 1.17 | 0.20 | 1.75 |
| 176 | proc | proc_proc_map_set | 0.15 | 0.15 | 1.25 | 0.19 | 1.74 |
| 177 | larceny | larceny_divrec | 0.15 | 0.17 | 1.23 | 0.18 | 1.73 |
| 178 | awfy | awfy_permute | 0.15 | 0.16 | 1.22 | 0.18 | 1.72 |
| 179 | lambda | csv_test | 0.22 | 0.24 | 0.92 | 0.34 | 1.71 |
| 180 | r7rs | r7rs_fibfp2 | 0.12 | 0.13 | 1.23 | 0.20 | 1.68 |
| 181 | larceny | larceny_diviter | 0.14 | 0.15 | 1.18 | 0.18 | 1.66 |
| 182 | awfy | awfy_mandelbrot | 0.20 | 0.21 | 1.05 | 0.19 | 1.66 |
| 183 | kostya | kostya_primes | 0.15 | 0.16 | 1.13 | 0.18 | 1.63 |
| 184 | lambda | parent_access | 0.10 | 0.10 | 1.21 | 0.21 | 1.61 |
| 185 | awfy | awfy_sieve2 | 0.15 | 0.16 | 1.09 | 0.19 | 1.60 |
| 186 | proc | proc_match_stam | 0.12 | 0.12 | 1.17 | 0.18 | 1.59 |
| 187 | proc | proc_test_io_module | 0.14 | 0.15 | 1.09 | 0.18 | 1.56 |
| 188 | larceny | larceny_primes | 0.15 | 0.16 | 1.07 | 0.18 | 1.56 |
| 189 | r7rs | r7rs_fibfp | 0.10 | 0.12 | 1.13 | 0.19 | 1.54 |
| 190 | proc | proc_proc_markup_mutation | 0.13 | 0.14 | 1.07 | 0.18 | 1.53 |
| 191 | proc | proc_proc_bitwise | 0.13 | 0.14 | 1.07 | 0.18 | 1.53 |
| 192 | lambda | error_union_param | 0.09 | 0.14 | 1.08 | 0.20 | 1.51 |
| 193 | kostya | kostya_collatz | 0.14 | 0.16 | 0.99 | 0.18 | 1.47 |
| 194 | lambda | name_member | 0.12 | 0.14 | 1.03 | 0.18 | 1.47 |
| 195 | lambda | sys_fn | 0.09 | 0.13 | 1.06 | 0.18 | 1.45 |
| 196 | awfy | awfy_sieve | 0.13 | 0.15 | 0.97 | 0.19 | 1.44 |
| 197 | proc | proc_proc_error | 0.13 | 0.15 | 0.96 | 0.19 | 1.43 |
| 198 | lambda | sort_advanced | 0.11 | 0.15 | 0.97 | 0.18 | 1.42 |
| 199 | r7rs | r7rs_cpstak2 | 0.14 | 0.16 | 0.91 | 0.18 | 1.39 |
| 200 | larceny | larceny_array1 | 0.14 | 0.14 | 0.89 | 0.17 | 1.34 |
| 201 | lambda | builtin_import_global | 0.08 | 0.14 | 0.93 | 0.19 | 1.34 |
| 202 | lambda | import_vars | 0.05 | 0.88 | 0.27 | 0.12 | 1.32 |
| 203 | r7rs | r7rs_tak2 | 0.14 | 0.15 | 0.86 | 0.17 | 1.32 |
| 204 | r7rs | r7rs_sumfp2 | 0.13 | 0.14 | 0.86 | 0.18 | 1.31 |
| 205 | r7rs | r7rs_ack2 | 0.13 | 0.15 | 0.84 | 0.18 | 1.30 |
| 206 | lambda | input_jsonld | 0.09 | 0.12 | 0.85 | 0.19 | 1.25 |
| 207 | lambda | builtin_import | 0.26 | 0.11 | 0.67 | 0.18 | 1.22 |
| 208 | lambda | reduce | 0.09 | 0.13 | 0.80 | 0.18 | 1.21 |
| 209 | r7rs | r7rs_cpstak | 0.12 | 0.14 | 0.75 | 0.18 | 1.19 |
| 210 | r7rs | r7rs_fib2 | 0.12 | 0.13 | 0.76 | 0.18 | 1.18 |
| 211 | r7rs | r7rs_sumfp | 0.11 | 0.13 | 0.75 | 0.18 | 1.17 |
| 212 | r7rs | r7rs_sum | 0.11 | 0.12 | 0.76 | 0.17 | 1.17 |
| 213 | r7rs | r7rs_tak | 0.12 | 0.14 | 0.73 | 0.18 | 1.17 |
| 214 | r7rs | r7rs_ack | 0.11 | 0.12 | 0.72 | 0.17 | 1.12 |
| 215 | r7rs | r7rs_fib | 0.10 | 0.15 | 0.64 | 0.18 | 1.07 |
| 216 | lambda | input_csv | 0.08 | 0.10 | 0.70 | 0.18 | 1.06 |
| 217 | lambda | parse | 0.07 | 0.10 | 0.55 | 0.17 | 0.89 |
| 218 | proc | proc_proc2 | 0.06 | 0.11 | 0.44 | 0.18 | 0.78 |
| 219 | lambda | type | 0.08 | 0.10 | 0.40 | 0.18 | 0.76 |
| 220 | lambda | input_dir | 0.07 | 0.10 | 0.39 | 0.17 | 0.73 |
| 221 | proc | proc_proc1 | 0.06 | 0.09 | 0.36 | 0.17 | 0.67 |
| 222 | lambda | single_let | 0.04 | 0.04 | 0.23 | 0.18 | 0.49 |
| 223 | lambda | single | 0.02 | 0.04 | 0.23 | 0.18 | 0.48 |

---

## Set 2: Scripts with Imports — 55 scripts

Timings include all imported modules. Module count shown in last column.


### Phase Summary (with imports)

| Phase | Total (ms) | Avg (ms) | % of Total |
|-------|-----------|----------|------------|
| Tree-sitter Parse | 135.73 | 2.47 | 1.6% |
| AST Build | 6828.18 | 124.15 | 79.7% |
| MIR Transpile | 1559.91 | 28.36 | 18.2% |
| JIT Codegen | 47.86 | 0.87 | 0.6% |
| **Total** | **8571.68** | **155.85** | 100% |

| Suite | Scripts | Parse (ms) | AST (ms) | Transpile (ms) | JIT (ms) | Total (ms) | Avg (ms) |
|-------|---------|-----------|----------|---------------|----------|-----------|----------|
| chart | 24 | 93.57 | 5361.79 | 1004.09 | 25.46 | 6484.92 | 270.20 |
| lambda | 6 | 0.60 | 8.34 | 5.69 | 1.83 | 16.46 | 2.74 |
| latex | 12 | 26.85 | 836.04 | 379.53 | 12.28 | 1254.70 | 104.56 |
| math | 13 | 14.71 | 622.01 | 170.59 | 8.29 | 815.60 | 62.74 |

### Per-Script Breakdown (with imports)

| # | Suite | Script | Parse (ms) | AST (ms) | Transpile (ms) | JIT (ms) | Total (ms) | Modules |
|---|-------|--------|-----------|----------|---------------|----------|-----------|---------|
| 1 | latex | latex_test_latex_m7 | 16.02 | 588.70 | 219.56 | 6.29 | 830.57 | 28 |
| 2 | math | math_test_math_html_output | 5.97 | 364.17 | 63.41 | 2.42 | 435.98 | 15 |
| 3 | chart | chart_test_histogram | 10.07 | 266.16 | 113.50 | 3.19 | 392.92 | 16 |
| 4 | chart | chart_test_rule_chart | 10.13 | 264.38 | 111.75 | 3.06 | 389.31 | 16 |
| 5 | chart | chart_test_bubble_chart | 9.54 | 265.50 | 108.57 | 2.86 | 386.46 | 15 |
| 6 | chart | chart_test_stacked_bar_chart | 9.63 | 263.99 | 108.24 | 2.87 | 384.73 | 15 |
| 7 | chart | chart_test_concat_chart | 9.81 | 260.36 | 109.70 | 2.92 | 382.80 | 15 |
| 8 | chart | chart_test_boxplot | 3.35 | 240.78 | 32.20 | 0.74 | 277.07 | 3 |
| 9 | chart | chart_test_stacked_area_chart | 3.72 | 240.38 | 31.96 | 0.76 | 276.83 | 3 |
| 10 | chart | chart_test_line_chart | 3.31 | 240.04 | 31.10 | 0.77 | 275.22 | 3 |
| 11 | chart | chart_test_errorbar | 3.03 | 240.00 | 31.35 | 0.76 | 275.15 | 3 |
| 12 | chart | chart_test_area_chart | 2.56 | 241.31 | 27.07 | 0.51 | 271.44 | 2 |
| 13 | chart | chart_test_annotation | 2.56 | 239.37 | 28.59 | 0.53 | 271.05 | 2 |
| 14 | chart | chart_test_theme_dark | 3.01 | 234.63 | 32.54 | 0.74 | 270.93 | 3 |
| 15 | chart | chart_test_temporal_axis | 3.08 | 234.21 | 32.89 | 0.74 | 270.91 | 3 |
| 16 | chart | chart_test_tick_chart | 2.58 | 239.71 | 27.37 | 0.49 | 270.15 | 2 |
| 17 | chart | chart_test_grouped_bar_chart | 2.52 | 239.07 | 28.05 | 0.49 | 270.13 | 2 |
| 18 | chart | chart_test_candlestick | 2.55 | 238.52 | 28.39 | 0.50 | 269.97 | 2 |
| 19 | chart | chart_test_scatter_chart | 2.60 | 239.10 | 27.48 | 0.53 | 269.71 | 2 |
| 20 | chart | chart_test_heatmap | 2.61 | 237.10 | 28.52 | 0.51 | 268.75 | 2 |
| 21 | chart | chart_test_text_chart | 2.72 | 237.64 | 27.31 | 0.50 | 268.18 | 2 |
| 22 | chart | chart_test_tooltip | 1.57 | 157.40 | 16.53 | 0.55 | 176.04 | 2 |
| 23 | chart | chart_test_bar_chart | 0.59 | 138.38 | 4.31 | 0.33 | 143.61 | 2 |
| 24 | chart | chart_test_layered_chart | 0.61 | 137.58 | 4.49 | 0.35 | 143.03 | 2 |
| 25 | chart | chart_test_conditional_color | 0.68 | 133.08 | 6.17 | 0.36 | 140.29 | 2 |
| 26 | chart | chart_test_facet_chart | 0.74 | 133.11 | 6.04 | 0.37 | 140.25 | 2 |
| 27 | latex | latex_test_latex_symbols | 1.47 | 62.27 | 51.20 | 0.80 | 115.75 | 4 |
| 28 | latex | latex_test_latex_analyze | 2.47 | 61.25 | 27.20 | 1.07 | 91.99 | 4 |
| 29 | math | math_test_math_atom_enclose | 1.69 | 50.01 | 16.99 | 0.85 | 69.55 | 6 |
| 30 | math | math_test_math_delimiters | 1.45 | 44.22 | 17.14 | 0.95 | 63.76 | 6 |
| 31 | math | math_test_math_atom_color | 1.21 | 40.07 | 14.63 | 0.96 | 56.87 | 6 |
| 32 | latex | latex_test_latex_macros | 1.59 | 29.98 | 17.95 | 0.75 | 50.27 | 3 |
| 33 | math | math_test_math_atom_spacing | 0.80 | 33.47 | 8.07 | 0.45 | 42.80 | 3 |
| 34 | math | math_test_math_atom_style | 0.46 | 37.18 | 3.55 | 0.27 | 41.46 | 2 |
| 35 | latex | latex_test_latex_spacing | 1.06 | 22.27 | 12.22 | 0.64 | 36.18 | 3 |
| 36 | latex | latex_test_latex_boxes | 0.65 | 24.43 | 8.14 | 0.31 | 33.54 | 2 |
| 37 | math | math_test_math_symbols | 0.65 | 15.53 | 16.49 | 0.41 | 33.09 | 2 |
| 38 | latex | latex_test_latex_color | 0.84 | 15.60 | 10.82 | 0.46 | 27.73 | 2 |
| 39 | latex | latex_test_latex_util | 0.92 | 10.39 | 12.34 | 0.48 | 24.14 | 2 |
| 40 | math | math_test_math_box | 0.40 | 15.10 | 5.61 | 0.34 | 21.45 | 2 |
| 41 | latex | latex_test_latex_includegraphics | 0.87 | 10.23 | 9.13 | 0.47 | 20.70 | 2 |
| 42 | math | math_test_math_context | 0.44 | 6.69 | 4.68 | 0.33 | 12.14 | 2 |
| 43 | math | math_test_math_css | 0.44 | 5.08 | 5.97 | 0.35 | 11.83 | 2 |
| 44 | latex | latex_test_latex_font_decl | 0.36 | 5.01 | 4.78 | 0.35 | 10.49 | 2 |
| 45 | latex | latex_test_latex_to_html | 0.42 | 4.79 | 4.61 | 0.37 | 10.19 | 2 |
| 46 | math | math_test_math_optimize | 0.47 | 3.48 | 5.42 | 0.33 | 9.70 | 2 |
| 47 | math | math_test_math_spacing | 0.38 | 3.47 | 4.64 | 0.32 | 8.81 | 2 |
| 48 | math | math_test_math_metrics | 0.35 | 3.52 | 3.99 | 0.31 | 8.18 | 2 |
| 49 | lambda | import_multi | 0.13 | 1.79 | 1.62 | 0.44 | 3.98 | 3 |
| 50 | latex | latex_test_latex_css | 0.17 | 1.10 | 1.59 | 0.28 | 3.15 | 2 |
| 51 | lambda | import_chain | 0.08 | 2.24 | 0.42 | 0.19 | 2.93 | 2 |
| 52 | lambda | import_types | 0.10 | 1.18 | 1.04 | 0.26 | 2.59 | 2 |
| 53 | lambda | import_compute | 0.10 | 1.21 | 0.92 | 0.30 | 2.52 | 2 |
| 54 | lambda | import_pub_types | 0.10 | 0.91 | 0.90 | 0.34 | 2.25 | 2 |
| 55 | lambda | import_error_destr | 0.10 | 1.01 | 0.79 | 0.30 | 2.19 | 2 |

---

## Skipped Scripts

| Script | Reason |
|--------|--------|
| awfy_cd2 | no profile data |
