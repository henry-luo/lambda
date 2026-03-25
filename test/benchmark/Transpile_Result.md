# Transpiling Time Profile Report

Scripts profiled: 277 (220 standalone, 57 with imports, 2 skipped)


---

## Set 1: Standalone Scripts (no imports) — 220 scripts

These timings reflect a single module and are the most direct measure of each compilation phase.


### Phase Summary (standalone)

| Phase | Total (ms) | Avg (ms) | % of Total |
|-------|-----------|----------|------------|
| Tree-sitter Parse | 56.03 | 0.25 | 6.0% |
| AST Build | 83.56 | 0.38 | 9.0% |
| MIR Transpile | 741.80 | 3.37 | 79.9% |
| JIT Codegen | 47.24 | 0.21 | 5.1% |
| **Total** | **928.63** | **4.22** | 100% |

| Suite | Scripts | Parse (ms) | AST (ms) | Transpile (ms) | JIT (ms) | Total (ms) | Avg (ms) |
|-------|---------|-----------|----------|---------------|----------|-----------|----------|
| awfy | 24 | 14.66 | 17.05 | 152.07 | 6.87 | 190.64 | 7.94 |
| beng | 7 | 1.93 | 2.02 | 23.08 | 1.44 | 28.47 | 4.07 |
| kostya | 6 | 1.20 | 1.33 | 12.98 | 1.17 | 16.68 | 2.78 |
| lambda | 113 | 25.23 | 32.11 | 424.16 | 24.49 | 506.00 | 4.48 |
| larceny | 14 | 3.00 | 3.46 | 32.18 | 2.75 | 41.39 | 2.96 |
| latex | 2 | 0.29 | 9.89 | 2.68 | 0.20 | 13.06 | 6.53 |
| math | 2 | 0.52 | 7.42 | 6.39 | 0.41 | 14.74 | 7.37 |
| proc | 32 | 6.34 | 7.06 | 62.30 | 6.29 | 82.00 | 2.56 |
| r7rs | 20 | 2.86 | 3.22 | 25.96 | 3.62 | 35.66 | 1.78 |

### Per-Script Breakdown (standalone)

| # | Suite | Script | Parse (ms) | AST (ms) | Transpile (ms) | JIT (ms) | Total (ms) |
|---|-------|--------|-----------|----------|---------------|----------|-----------|
| 1 | lambda | complex_iot_report_html | 2.16 | 2.68 | 48.34 | 0.65 | 53.80 |
| 2 | awfy | awfy_cd | 2.08 | 2.65 | 30.02 | 0.70 | 35.44 |
| 3 | lambda | type_pattern | 1.43 | 2.14 | 22.67 | 0.44 | 26.69 |
| 4 | awfy | awfy_deltablue2 | 2.14 | 2.47 | 18.21 | 0.58 | 23.40 |
| 5 | awfy | awfy_deltablue | 1.88 | 2.16 | 16.91 | 0.52 | 21.50 |
| 6 | awfy | awfy_havlak2 | 1.64 | 2.06 | 14.22 | 0.54 | 18.46 |
| 7 | awfy | awfy_havlak | 1.69 | 2.02 | 13.94 | 0.53 | 18.18 |
| 8 | lambda | vector_sys_func | 0.64 | 0.94 | 12.25 | 0.32 | 14.18 |
| 9 | lambda | expr_stam | 0.66 | 0.62 | 11.76 | 0.33 | 13.36 |
| 10 | lambda | pipe_where | 0.24 | 0.28 | 11.76 | 0.32 | 12.61 |
| 11 | lambda | for_clauses_test | 0.47 | 0.54 | 10.85 | 0.32 | 12.19 |
| 12 | lambda | numeric_sys_func | 0.40 | 0.54 | 10.57 | 0.31 | 11.83 |
| 13 | proc | proc_proc_fill | 0.57 | 0.63 | 9.68 | 0.37 | 11.27 |
| 14 | awfy | awfy_nbody | 0.37 | 0.43 | 10.20 | 0.26 | 11.25 |
| 15 | lambda | comp_expr | 0.46 | 0.53 | 9.68 | 0.31 | 10.97 |
| 16 | latex | latex_test_latex_includegraphics | 0.17 | 9.07 | 1.43 | 0.10 | 10.77 |
| 17 | awfy | awfy_richards2 | 0.90 | 0.98 | 8.20 | 0.33 | 10.43 |
| 18 | awfy | awfy_richards | 0.87 | 0.95 | 7.97 | 0.34 | 10.13 |
| 19 | lambda | numeric_expr | 0.34 | 0.43 | 8.39 | 0.31 | 9.47 |
| 20 | lambda | comp_expr_edge | 0.30 | 0.33 | 7.31 | 0.27 | 8.20 |
| 21 | math | math_test_math_spacing | 0.34 | 3.23 | 4.17 | 0.30 | 8.10 |
| 22 | beng | beng_nbody | 0.45 | 0.50 | 6.83 | 0.25 | 8.03 |
| 23 | awfy | awfy_nbody2 | 0.42 | 0.45 | 6.62 | 0.24 | 7.71 |
| 24 | lambda | decimal | 0.24 | 0.32 | 6.85 | 0.27 | 7.68 |
| 25 | lambda | type_occurrence | 0.40 | 0.55 | 6.35 | 0.24 | 7.54 |
| 26 | lambda | expr | 0.38 | 0.42 | 6.47 | 0.25 | 7.53 |
| 27 | lambda | string_pattern | 0.34 | 0.47 | 6.15 | 0.25 | 7.24 |
| 28 | lambda | vector_performance | 0.35 | 0.50 | 5.99 | 0.23 | 7.08 |
| 29 | math | math_test_math_css | 0.17 | 4.18 | 2.23 | 0.11 | 6.69 |
| 30 | lambda | vector_advanced | 0.34 | 0.46 | 5.61 | 0.24 | 6.64 |
| 31 | lambda | error_propagation | 0.41 | 0.54 | 5.24 | 0.30 | 6.56 |
| 32 | lambda | pipe_spread | 0.17 | 0.20 | 5.92 | 0.26 | 6.52 |
| 33 | lambda | string_funcs | 0.26 | 0.33 | 5.53 | 0.23 | 6.32 |
| 34 | lambda | box_unbox_advanced | 0.39 | 0.52 | 4.96 | 0.28 | 6.12 |
| 35 | larceny | larceny_paraffins | 0.37 | 0.41 | 4.91 | 0.23 | 5.92 |
| 36 | lambda | for_decompose | 0.25 | 0.34 | 5.09 | 0.23 | 5.91 |
| 37 | lambda | path | 0.28 | 0.33 | 4.87 | 0.22 | 5.69 |
| 38 | beng | beng_knucleotide | 0.37 | 0.37 | 4.70 | 0.23 | 5.66 |
| 39 | lambda | match_expr | 0.30 | 0.34 | 4.76 | 0.25 | 5.64 |
| 40 | lambda | datetime | 0.39 | 0.39 | 4.63 | 0.22 | 5.64 |
| 41 | lambda | sys_func_native_cmp | 0.32 | 0.40 | 4.58 | 0.26 | 5.56 |
| 42 | lambda | constrained_type | 0.18 | 0.21 | 4.91 | 0.24 | 5.53 |
| 43 | larceny | larceny_triangl | 0.31 | 0.46 | 4.51 | 0.23 | 5.51 |
| 44 | lambda | vector_basic | 0.24 | 0.38 | 4.62 | 0.24 | 5.49 |
| 45 | lambda | simple_expr | 0.30 | 0.36 | 4.58 | 0.23 | 5.48 |
| 46 | lambda | func_param2 | 0.29 | 0.41 | 4.53 | 0.24 | 5.45 |
| 47 | lambda | structural_equality | 0.23 | 0.26 | 4.68 | 0.24 | 5.39 |
| 48 | lambda | test_string_pattern_integration | 0.30 | 0.36 | 4.07 | 0.22 | 4.94 |
| 49 | kostya | kostya_base64 | 0.26 | 0.32 | 4.07 | 0.22 | 4.86 |
| 50 | lambda | string_indexable | 0.20 | 0.24 | 4.16 | 0.23 | 4.83 |
| 51 | lambda | namespace | 0.26 | 0.32 | 4.00 | 0.22 | 4.80 |
| 52 | larceny | larceny_deriv2 | 0.31 | 0.34 | 3.81 | 0.23 | 4.69 |
| 53 | awfy | awfy_bounce2 | 0.23 | 0.24 | 3.97 | 0.21 | 4.69 |
| 54 | awfy | awfy_bounce | 0.22 | 0.24 | 3.88 | 0.22 | 4.58 |
| 55 | lambda | int64 | 0.24 | 0.31 | 3.73 | 0.22 | 4.49 |
| 56 | lambda | transpile_typed_closure | 0.24 | 0.30 | 3.62 | 0.26 | 4.42 |
| 57 | lambda | value | 0.20 | 0.24 | 3.71 | 0.22 | 4.37 |
| 58 | lambda | that_implicit_name | 0.14 | 0.16 | 3.85 | 0.22 | 4.36 |
| 59 | lambda | pipe_type_infer | 0.14 | 0.18 | 3.72 | 0.21 | 4.28 |
| 60 | proc | proc_proc_typed_array_param | 0.30 | 0.32 | 3.42 | 0.23 | 4.27 |
| 61 | lambda | func | 0.20 | 0.25 | 3.56 | 0.23 | 4.24 |
| 62 | lambda | for_element_spread | 0.16 | 0.21 | 3.49 | 0.21 | 4.08 |
| 63 | lambda | closure_advanced | 0.22 | 0.31 | 3.28 | 0.24 | 4.05 |
| 64 | proc | proc_proc_array_type_convert | 0.23 | 0.27 | 3.29 | 0.23 | 4.01 |
| 65 | lambda | transpile_bitwise | 0.27 | 0.32 | 3.18 | 0.23 | 4.00 |
| 66 | r7rs | r7rs_nqueens2 | 0.27 | 0.27 | 3.12 | 0.21 | 3.88 |
| 67 | lambda | vmap | 0.20 | 0.27 | 3.16 | 0.19 | 3.83 |
| 68 | beng | beng_fannkuch | 0.21 | 0.23 | 3.13 | 0.20 | 3.80 |
| 69 | lambda | let_for_array | 0.24 | 0.27 | 2.90 | 0.21 | 3.61 |
| 70 | lambda | tail_call | 0.21 | 0.24 | 2.87 | 0.22 | 3.56 |
| 71 | r7rs | r7rs_fft | 0.21 | 0.24 | 2.86 | 0.19 | 3.50 |
| 72 | lambda | for_element_filter | 0.14 | 0.18 | 2.98 | 0.20 | 3.48 |
| 73 | lambda | is_in_precedence | 0.23 | 0.32 | 2.69 | 0.21 | 3.46 |
| 74 | lambda | transpile_error_ret_types | 0.25 | 0.29 | 2.62 | 0.27 | 3.44 |
| 75 | proc | proc_proc_var | 0.35 | 0.38 | 2.48 | 0.22 | 3.43 |
| 76 | r7rs | r7rs_fft2 | 0.25 | 0.26 | 2.70 | 0.20 | 3.43 |
| 77 | lambda | sys_func_native_math | 0.24 | 0.29 | 2.63 | 0.23 | 3.39 |
| 78 | larceny | larceny_pnpoly | 0.22 | 0.29 | 2.68 | 0.20 | 3.38 |
| 79 | kostya | kostya_levenshtein | 0.24 | 0.26 | 2.67 | 0.20 | 3.37 |
| 80 | lambda | decimal_big | 0.12 | 0.15 | 2.90 | 0.21 | 3.37 |
| 81 | beng | beng_spectralnorm | 0.25 | 0.27 | 2.62 | 0.21 | 3.35 |
| 82 | r7rs | r7rs_nqueens | 0.24 | 0.26 | 2.61 | 0.20 | 3.33 |
| 83 | lambda | large_int_map | 0.21 | 0.22 | 2.67 | 0.21 | 3.30 |
| 84 | lambda | comment | 0.18 | 0.23 | 2.65 | 0.21 | 3.24 |
| 85 | lambda | null_safe_member | 0.19 | 0.27 | 2.59 | 0.19 | 3.24 |
| 86 | proc | proc_proc_element_mutation | 0.24 | 0.25 | 2.48 | 0.21 | 3.19 |
| 87 | lambda | type2 | 0.16 | 0.18 | 2.63 | 0.20 | 3.17 |
| 88 | larceny | larceny_ray | 0.27 | 0.31 | 2.37 | 0.19 | 3.14 |
| 89 | lambda | child_query | 0.21 | 0.28 | 2.45 | 0.19 | 3.14 |
| 90 | beng | beng_regexredux | 0.28 | 0.25 | 2.43 | 0.18 | 3.13 |
| 91 | lambda | sys_func_is_nan | 0.13 | 0.16 | 2.62 | 0.20 | 3.11 |
| 92 | proc | proc_proc_param_type_infer | 0.26 | 0.27 | 2.33 | 0.20 | 3.08 |
| 93 | proc | proc_proc_array_set | 0.17 | 0.20 | 2.50 | 0.21 | 3.08 |
| 94 | lambda | array_float | 0.25 | 0.41 | 2.24 | 0.18 | 3.07 |
| 95 | kostya | kostya_json_gen | 0.23 | 0.25 | 2.36 | 0.21 | 3.04 |
| 96 | lambda | sys_func_math_extended | 0.24 | 0.25 | 2.33 | 0.21 | 3.03 |
| 97 | lambda | type_negation | 0.16 | 0.19 | 2.50 | 0.18 | 3.03 |
| 98 | lambda | unboxed_field_access | 0.17 | 0.20 | 2.48 | 0.20 | 3.03 |
| 99 | lambda | unboxed_sys_func | 0.17 | 0.22 | 2.41 | 0.21 | 3.00 |
| 100 | proc | proc_vmap | 0.24 | 0.25 | 2.27 | 0.20 | 2.96 |
| 101 | proc | proc_proc_map_type_change | 0.25 | 0.26 | 2.15 | 0.20 | 2.86 |
| 102 | lambda | query | 0.17 | 0.23 | 2.26 | 0.20 | 2.86 |
| 103 | proc | proc_proc_arr_concat | 0.17 | 0.20 | 2.30 | 0.19 | 2.86 |
| 104 | larceny | larceny_deriv | 0.24 | 0.27 | 2.13 | 0.20 | 2.85 |
| 105 | lambda | transpile_idiv_mod | 0.18 | 0.24 | 2.17 | 0.20 | 2.80 |
| 106 | lambda | in_container | 0.10 | 0.16 | 2.31 | 0.19 | 2.76 |
| 107 | proc | proc_while_swap | 0.33 | 0.35 | 1.86 | 0.19 | 2.75 |
| 108 | lambda | string_prefix_suffix | 0.13 | 0.17 | 2.24 | 0.19 | 2.74 |
| 109 | awfy | awfy_queens2 | 0.22 | 0.22 | 2.08 | 0.21 | 2.73 |
| 110 | larceny | larceny_gcbench | 0.20 | 0.20 | 2.10 | 0.20 | 2.70 |
| 111 | proc | proc_test_pipe_file | 0.21 | 0.25 | 2.07 | 0.19 | 2.70 |
| 112 | larceny | larceny_quicksort | 0.19 | 0.21 | 2.06 | 0.20 | 2.66 |
| 113 | awfy | awfy_towers2 | 0.23 | 0.23 | 1.98 | 0.20 | 2.64 |
| 114 | lambda | closure | 0.18 | 0.23 | 1.98 | 0.20 | 2.59 |
| 115 | lambda | string_pattern_ops | 0.15 | 0.20 | 2.04 | 0.19 | 2.58 |
| 116 | proc | proc_proc_closure_mutation | 0.20 | 0.25 | 1.91 | 0.21 | 2.56 |
| 117 | awfy | awfy_queens | 0.18 | 0.19 | 1.99 | 0.19 | 2.55 |
| 118 | proc | proc_proc_semicolon | 0.25 | 0.30 | 1.76 | 0.20 | 2.53 |
| 119 | proc | proc_proc_else_if | 0.25 | 0.25 | 1.84 | 0.18 | 2.52 |
| 120 | beng | beng_binarytrees | 0.19 | 0.20 | 1.92 | 0.19 | 2.49 |
| 121 | lambda | transpile_len_typed | 0.17 | 0.21 | 1.90 | 0.19 | 2.47 |
| 122 | lambda | method_call | 0.15 | 0.21 | 1.89 | 0.18 | 2.42 |
| 123 | lambda | nested_shadowing | 0.15 | 0.18 | 1.87 | 0.20 | 2.39 |
| 124 | lambda | empty_string_null | 0.12 | 0.15 | 1.93 | 0.18 | 2.39 |
| 125 | r7rs | r7rs_mbrot2 | 0.22 | 0.23 | 1.76 | 0.19 | 2.38 |
| 126 | proc | proc_tail_call_proc | 0.19 | 0.20 | 1.79 | 0.20 | 2.38 |
| 127 | kostya | kostya_matmul | 0.19 | 0.20 | 1.79 | 0.19 | 2.38 |
| 128 | latex | latex_test_latex_css | 0.11 | 0.82 | 1.25 | 0.10 | 2.36 |
| 129 | lambda | float_conversion | 0.15 | 0.19 | 1.81 | 0.20 | 2.35 |
| 130 | lambda | error_handling | 0.18 | 0.22 | 1.70 | 0.22 | 2.32 |
| 131 | awfy | awfy_towers | 0.20 | 0.22 | 1.69 | 0.19 | 2.31 |
| 132 | larceny | larceny_gcbench2 | 0.19 | 0.20 | 1.71 | 0.19 | 2.27 |
| 133 | proc | proc_proc_control | 0.23 | 0.25 | 1.58 | 0.19 | 2.27 |
| 134 | lambda | pipe_sysfunc | 0.11 | 0.15 | 1.77 | 0.19 | 2.22 |
| 135 | lambda | spread | 0.12 | 0.17 | 1.72 | 0.18 | 2.20 |
| 136 | larceny | larceny_puzzle | 0.16 | 0.18 | 1.67 | 0.18 | 2.19 |
| 137 | proc | proc_proc_dir_listing | 0.17 | 0.16 | 1.68 | 0.17 | 2.19 |
| 138 | proc | proc_proc_bitwise_int64 | 0.17 | 0.20 | 1.64 | 0.18 | 2.19 |
| 139 | r7rs | r7rs_mbrot | 0.19 | 0.21 | 1.58 | 0.18 | 2.18 |
| 140 | lambda | first_class_fn | 0.14 | 0.16 | 1.66 | 0.19 | 2.17 |
| 141 | lambda | string | 0.10 | 0.13 | 1.74 | 0.18 | 2.14 |
| 142 | proc | proc_proc_param_mutation | 0.16 | 0.17 | 1.62 | 0.19 | 2.12 |
| 143 | lambda | mixed_numeric_ops | 0.12 | 0.15 | 1.64 | 0.19 | 2.11 |
| 144 | lambda | namespace_v2 | 0.16 | 0.19 | 1.54 | 0.18 | 2.07 |
| 145 | lambda | string_ord_chr | 0.10 | 0.14 | 1.65 | 0.17 | 2.06 |
| 146 | lambda | chained_comparisons | 0.12 | 0.11 | 1.65 | 0.19 | 2.06 |
| 147 | lambda | match_string_pattern | 0.13 | 0.17 | 1.55 | 0.19 | 2.04 |
| 148 | lambda | sys_func_math_constants | 0.16 | 0.18 | 1.52 | 0.18 | 2.04 |
| 149 | lambda | typed_map_direct_access | 0.14 | 0.15 | 1.54 | 0.18 | 2.03 |
| 150 | beng | beng_mandelbrot | 0.19 | 0.21 | 1.46 | 0.17 | 2.03 |
| 151 | lambda | box_unbox_negative | 0.13 | 0.16 | 1.51 | 0.18 | 1.97 |
| 152 | awfy | awfy_permute2 | 0.18 | 0.18 | 1.33 | 0.19 | 1.89 |
| 153 | lambda | if_expr_types | 0.13 | 0.14 | 1.44 | 0.18 | 1.89 |
| 154 | lambda | trim | 0.11 | 0.14 | 1.46 | 0.18 | 1.87 |
| 155 | proc | proc_proc_cmd | 0.14 | 0.16 | 1.39 | 0.19 | 1.87 |
| 156 | lambda | split_null_concat | 0.10 | 0.14 | 1.45 | 0.17 | 1.87 |
| 157 | proc | proc_proc_div_mod | 0.15 | 0.18 | 1.38 | 0.18 | 1.87 |
| 158 | lambda | correlation_math | 0.12 | 0.16 | 1.44 | 0.16 | 1.86 |
| 159 | lambda | typed_param_string | 0.11 | 0.14 | 1.39 | 0.18 | 1.82 |
| 160 | awfy | awfy_storage2 | 0.16 | 0.18 | 1.31 | 0.18 | 1.81 |
| 161 | lambda | map_spread_override | 0.15 | 0.18 | 1.28 | 0.18 | 1.80 |
| 162 | lambda | func_param | 0.14 | 0.17 | 1.22 | 0.19 | 1.75 |
| 163 | awfy | awfy_mandelbrot2 | 0.20 | 0.22 | 1.14 | 0.18 | 1.73 |
| 164 | larceny | larceny_divrec | 0.14 | 0.15 | 1.20 | 0.19 | 1.70 |
| 165 | awfy | awfy_permute | 0.15 | 0.16 | 1.19 | 0.18 | 1.69 |
| 166 | awfy | awfy_storage | 0.15 | 0.16 | 1.16 | 0.18 | 1.64 |
| 167 | lambda | box_unbox | 0.09 | 0.13 | 1.23 | 0.17 | 1.63 |
| 168 | proc | proc_proc_var_type_widen | 0.14 | 0.19 | 1.11 | 0.18 | 1.62 |
| 169 | larceny | larceny_diviter | 0.13 | 0.14 | 1.15 | 0.18 | 1.61 |
| 170 | r7rs | r7rs_fibfp2 | 0.10 | 0.12 | 1.18 | 0.18 | 1.59 |
| 171 | awfy | awfy_mandelbrot | 0.17 | 0.21 | 1.03 | 0.17 | 1.57 |
| 172 | proc | proc_proc_map_set | 0.13 | 0.14 | 1.08 | 0.19 | 1.56 |
| 173 | lambda | forward_ref | 0.10 | 0.13 | 1.13 | 0.19 | 1.56 |
| 174 | awfy | awfy_list | 0.15 | 0.16 | 1.08 | 0.18 | 1.56 |
| 175 | kostya | kostya_primes | 0.14 | 0.15 | 1.10 | 0.17 | 1.56 |
| 176 | lambda | robust_coverage | 0.12 | 0.12 | 1.11 | 0.17 | 1.52 |
| 177 | lambda | math_random | 0.11 | 0.15 | 1.08 | 0.17 | 1.51 |
| 178 | r7rs | r7rs_fibfp | 0.09 | 0.11 | 1.11 | 0.18 | 1.50 |
| 179 | proc | proc_test_io_module | 0.13 | 0.14 | 1.05 | 0.18 | 1.50 |
| 180 | awfy | awfy_sieve2 | 0.12 | 0.14 | 1.05 | 0.17 | 1.49 |
| 181 | proc | proc_match_stam | 0.12 | 0.12 | 1.07 | 0.17 | 1.48 |
| 182 | larceny | larceny_primes | 0.14 | 0.14 | 1.03 | 0.17 | 1.48 |
| 183 | kostya | kostya_collatz | 0.14 | 0.16 | 0.99 | 0.17 | 1.47 |
| 184 | proc | proc_proc_markup_mutation | 0.13 | 0.14 | 1.01 | 0.18 | 1.46 |
| 185 | lambda | parent_access | 0.10 | 0.10 | 1.07 | 0.18 | 1.44 |
| 186 | proc | proc_proc_bitwise | 0.11 | 0.13 | 1.00 | 0.17 | 1.41 |
| 187 | lambda | name_member | 0.11 | 0.13 | 0.99 | 0.18 | 1.41 |
| 188 | proc | proc_proc_error | 0.12 | 0.14 | 0.94 | 0.18 | 1.38 |
| 189 | proc | proc_clock | 0.13 | 0.15 | 0.88 | 0.18 | 1.35 |
| 190 | awfy | awfy_sieve | 0.12 | 0.13 | 0.91 | 0.17 | 1.34 |
| 191 | r7rs | r7rs_tak2 | 0.13 | 0.14 | 0.87 | 0.17 | 1.31 |
| 192 | lambda | sys_fn | 0.07 | 0.11 | 0.96 | 0.16 | 1.31 |
| 193 | r7rs | r7rs_sum2 | 0.12 | 0.13 | 0.88 | 0.18 | 1.30 |
| 194 | lambda | error_union_param | 0.08 | 0.11 | 0.93 | 0.18 | 1.29 |
| 195 | r7rs | r7rs_cpstak2 | 0.13 | 0.15 | 0.84 | 0.17 | 1.28 |
| 196 | lambda | builtin_import_alias | 0.10 | 0.12 | 0.88 | 0.17 | 1.27 |
| 197 | larceny | larceny_array1 | 0.13 | 0.14 | 0.85 | 0.16 | 1.26 |
| 198 | lambda | sort_advanced | 0.09 | 0.12 | 0.89 | 0.17 | 1.26 |
| 199 | r7rs | r7rs_sumfp2 | 0.12 | 0.13 | 0.82 | 0.17 | 1.24 |
| 200 | r7rs | r7rs_ack2 | 0.12 | 0.13 | 0.81 | 0.17 | 1.22 |
| 201 | lambda | builtin_import_global | 0.07 | 0.11 | 0.85 | 0.16 | 1.20 |
| 202 | r7rs | r7rs_sumfp | 0.10 | 0.13 | 0.74 | 0.18 | 1.16 |
| 203 | r7rs | r7rs_cpstak | 0.11 | 0.13 | 0.72 | 0.17 | 1.14 |
| 204 | lambda | reduce | 0.08 | 0.12 | 0.74 | 0.17 | 1.12 |
| 205 | lambda | input_jsonld | 0.08 | 0.11 | 0.77 | 0.17 | 1.11 |
| 206 | r7rs | r7rs_fib2 | 0.10 | 0.12 | 0.71 | 0.17 | 1.11 |
| 207 | lambda | csv_test | 0.07 | 0.11 | 0.75 | 0.17 | 1.09 |
| 208 | r7rs | r7rs_ack | 0.10 | 0.12 | 0.67 | 0.18 | 1.08 |
| 209 | r7rs | r7rs_tak | 0.10 | 0.12 | 0.69 | 0.16 | 1.08 |
| 210 | r7rs | r7rs_sum | 0.09 | 0.11 | 0.71 | 0.16 | 1.07 |
| 211 | lambda | builtin_import | 0.08 | 0.10 | 0.67 | 0.17 | 1.02 |
| 212 | lambda | input_csv | 0.08 | 0.10 | 0.67 | 0.17 | 1.01 |
| 213 | r7rs | r7rs_fib | 0.08 | 0.11 | 0.58 | 0.17 | 0.95 |
| 214 | lambda | parse | 0.07 | 0.10 | 0.53 | 0.18 | 0.86 |
| 215 | proc | proc_proc2 | 0.05 | 0.09 | 0.38 | 0.16 | 0.67 |
| 216 | lambda | type | 0.08 | 0.09 | 0.34 | 0.16 | 0.67 |
| 217 | lambda | input_dir | 0.05 | 0.08 | 0.36 | 0.16 | 0.66 |
| 218 | proc | proc_proc1 | 0.05 | 0.09 | 0.35 | 0.17 | 0.66 |
| 219 | lambda | single_let | 0.03 | 0.05 | 0.20 | 0.17 | 0.44 |
| 220 | lambda | single | 0.02 | 0.03 | 0.19 | 0.17 | 0.40 |

---

## Set 2: Scripts with Imports — 57 scripts

Timings include all imported modules. Module count shown in last column.


### Phase Summary (with imports)

| Phase | Total (ms) | Avg (ms) | % of Total |
|-------|-----------|----------|------------|
| Tree-sitter Parse | 266.31 | 4.67 | 2.3% |
| AST Build | 7925.74 | 139.05 | 69.7% |
| MIR Transpile | 3098.06 | 54.35 | 27.2% |
| JIT Codegen | 88.67 | 1.56 | 0.8% |
| **Total** | **11378.79** | **199.63** | 100% |

| Suite | Scripts | Parse (ms) | AST (ms) | Transpile (ms) | JIT (ms) | Total (ms) | Avg (ms) |
|-------|---------|-----------|----------|---------------|----------|-----------|----------|
| chart | 25 | 219.65 | 5633.70 | 2441.90 | 65.48 | 8360.72 | 334.43 |
| lambda | 9 | 0.92 | 14.13 | 11.62 | 2.53 | 29.20 | 3.24 |
| latex | 11 | 21.36 | 1193.87 | 318.16 | 8.77 | 1542.16 | 140.20 |
| math | 12 | 24.39 | 1084.04 | 326.39 | 11.89 | 1446.72 | 120.56 |

### Per-Script Breakdown (with imports)

| # | Suite | Script | Parse (ms) | AST (ms) | Transpile (ms) | JIT (ms) | Total (ms) | Modules |
|---|-------|--------|-----------|----------|---------------|----------|-----------|---------|
| 1 | latex | latex_test_latex_m7 | 11.65 | 944.46 | 171.82 | 4.44 | 1132.37 | 30 |
| 2 | math | math_test_math_html_output | 15.71 | 849.04 | 218.50 | 5.99 | 1089.24 | 32 |
| 3 | chart | chart_test_rule_chart | 9.56 | 254.50 | 106.08 | 2.78 | 371.31 | 16 |
| 4 | chart | chart_test_text_chart | 9.79 | 251.86 | 106.56 | 2.88 | 371.17 | 16 |
| 5 | chart | chart_test_stacked_bar_chart | 9.77 | 251.06 | 106.98 | 2.85 | 370.46 | 16 |
| 6 | chart | chart_test_errorbar | 9.53 | 251.02 | 106.96 | 2.87 | 370.38 | 15 |
| 7 | chart | chart_test_layered_chart | 9.67 | 250.61 | 106.82 | 2.83 | 370.14 | 16 |
| 8 | chart | chart_test_bubble_chart | 9.83 | 251.60 | 106.50 | 2.80 | 368.92 | 16 |
| 9 | chart | chart_test_stacked_area_chart | 9.63 | 249.00 | 106.35 | 2.78 | 367.76 | 14 |
| 10 | chart | chart_test_area_chart | 9.60 | 251.93 | 105.12 | 2.76 | 367.41 | 16 |
| 11 | chart | chart_test_boxplot | 9.25 | 250.98 | 103.82 | 2.63 | 367.17 | 16 |
| 12 | chart | chart_test_heatmap | 9.63 | 250.09 | 104.40 | 2.78 | 366.52 | 16 |
| 13 | chart | chart_test_tick_chart | 9.24 | 251.01 | 103.13 | 2.61 | 365.99 | 15 |
| 14 | chart | chart_test_candlestick | 9.26 | 249.30 | 103.44 | 2.69 | 365.63 | 15 |
| 15 | chart | chart_test_line_chart | 9.63 | 249.45 | 104.06 | 2.74 | 363.37 | 16 |
| 16 | chart | chart_test_histogram | 9.15 | 247.77 | 102.22 | 2.59 | 361.73 | 16 |
| 17 | chart | chart_test_bar_chart | 9.11 | 245.90 | 101.51 | 2.64 | 361.02 | 15 |
| 18 | chart | chart_test_scatter_chart | 9.07 | 246.02 | 101.21 | 2.58 | 358.84 | 15 |
| 19 | chart | chart_test_grouped_bar_chart | 9.09 | 245.57 | 101.21 | 2.57 | 358.44 | 16 |
| 20 | chart | chart_test_temporal_axis | 9.30 | 243.91 | 102.05 | 2.69 | 358.01 | 14 |
| 21 | chart | chart_test_annotation | 8.69 | 245.82 | 99.08 | 2.43 | 356.02 | 15 |
| 22 | chart | chart_test_tooltip | 6.90 | 150.46 | 78.17 | 2.39 | 237.92 | 14 |
| 23 | chart | chart_test_conditional_color | 6.80 | 149.60 | 78.09 | 2.34 | 236.83 | 14 |
| 24 | chart | chart_test_concat_chart | 6.83 | 149.35 | 78.14 | 2.33 | 236.64 | 14 |
| 25 | chart | chart_test_repeat_chart | 6.80 | 148.79 | 77.49 | 2.32 | 235.41 | 14 |
| 26 | chart | chart_test_facet_chart | 6.76 | 149.12 | 76.29 | 2.29 | 231.91 | 13 |
| 27 | chart | chart_test_theme_dark | 6.75 | 148.97 | 76.23 | 2.28 | 231.88 | 13 |
| 28 | latex | latex_test_latex_symbols | 1.41 | 62.35 | 51.90 | 0.73 | 116.36 | 4 |
| 29 | latex | latex_test_latex_analyze | 1.65 | 57.03 | 15.93 | 0.45 | 75.06 | 5 |
| 30 | math | math_test_math_atom_color | 1.16 | 38.90 | 14.00 | 0.87 | 54.93 | 6 |
| 31 | math | math_test_math_atom_enclose | 1.11 | 38.32 | 13.97 | 0.74 | 54.07 | 5 |
| 32 | math | math_test_math_atom_style | 1.08 | 36.27 | 12.43 | 0.76 | 50.56 | 6 |
| 33 | latex | latex_test_latex_picture | 1.82 | 25.17 | 21.91 | 0.59 | 49.56 | 2 |
| 34 | math | math_test_math_delimiters | 0.99 | 32.66 | 12.53 | 0.71 | 46.60 | 5 |
| 35 | latex | latex_test_latex_macros | 1.43 | 27.46 | 16.45 | 0.67 | 46.01 | 3 |
| 36 | math | math_test_math_atom_spacing | 0.86 | 31.25 | 11.45 | 0.67 | 41.16 | 4 |
| 37 | math | math_test_math_box | 0.94 | 23.33 | 9.61 | 0.58 | 34.36 | 4 |
| 38 | math | math_test_math_symbols | 0.64 | 14.67 | 15.76 | 0.38 | 31.49 | 2 |
| 39 | latex | latex_test_latex_color | 0.60 | 22.63 | 6.75 | 0.29 | 30.22 | 2 |
| 40 | latex | latex_test_latex_boxes | 0.62 | 14.86 | 7.69 | 0.29 | 26.83 | 2 |
| 41 | latex | latex_test_latex_spacing | 0.47 | 20.83 | 4.60 | 0.30 | 26.21 | 2 |
| 42 | latex | latex_test_latex_util | 0.88 | 9.74 | 11.53 | 0.44 | 22.61 | 2 |
| 43 | math | math_test_math_context | 0.63 | 9.26 | 6.43 | 0.43 | 16.74 | 3 |
| 44 | latex | latex_test_latex_font_decl | 0.43 | 4.74 | 5.22 | 0.25 | 10.57 | 3 |
| 45 | latex | latex_test_latex_to_html | 0.40 | 4.60 | 4.36 | 0.32 | 9.67 | 2 |
| 46 | lambda | import | 0.24 | 4.58 | 4.23 | 0.34 | 9.39 | 2 |
| 47 | math | math_test_math_optimize | 0.45 | 3.36 | 5.15 | 0.32 | 9.29 | 2 |
| 48 | math | math_test_math_util | 0.48 | 3.54 | 2.62 | 0.14 | 8.19 | 2 |
| 49 | math | math_test_math_metrics | 0.34 | 3.45 | 3.95 | 0.31 | 8.11 | 2 |
| 50 | lambda | import_multi | 0.10 | 1.61 | 1.45 | 0.34 | 3.52 | 3 |
| 51 | lambda | import_chain | 0.08 | 2.08 | 0.82 | 0.32 | 3.35 | 3 |
| 52 | lambda | import_arrays | 0.11 | 1.25 | 1.20 | 0.26 | 2.83 | 2 |
| 53 | lambda | import_types | 0.09 | 1.08 | 0.95 | 0.25 | 2.36 | 2 |
| 54 | lambda | import_compute | 0.07 | 1.12 | 0.87 | 0.24 | 2.33 | 2 |
| 55 | lambda | import_error_destr | 0.08 | 0.89 | 0.68 | 0.27 | 1.90 | 2 |
| 56 | lambda | import_vars | 0.07 | 0.80 | 0.69 | 0.25 | 1.81 | 2 |
| 57 | lambda | import_pub_types | 0.08 | 0.73 | 0.72 | 0.25 | 1.73 | 2 |

---

## Skipped Scripts

| Script | Reason |
|--------|--------|
| awfy_cd2 | no profile data |
| kostya_brainfuck | timeout |
