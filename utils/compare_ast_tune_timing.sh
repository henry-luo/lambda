#!/bin/sh
set -eu

baseline=""
candidate=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --baseline) baseline=$2; shift 2 ;;
        --candidate) candidate=$2; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
[ -d "$baseline" ] || { echo "missing baseline directory: $baseline" >&2; exit 1; }
[ -d "$candidate" ] || { echo "missing candidate directory: $candidate" >&2; exit 1; }

median() {
    sort -n "$1" | awk '{ a[NR] = $1 } END { if (NR == 0) exit 1; if (NR % 2) print a[(NR+1)/2]; else print (a[NR/2] + a[NR/2+1]) / 2 }'
}

compare_suite() {
    suite=$1
    bdir="$baseline/$suite/runs"
    cdir="$candidate/$suite/runs"
    [ -d "$bdir" ] && [ -d "$cdir" ] || { echo "missing suite runs for $suite" >&2; return 1; }
    btmp="./temp/ast_tune/compare_${suite}_baseline.$$"
    ctmp="./temp/ast_tune/compare_${suite}_candidate.$$"
    trap 'rm -f "$btmp" "$ctmp"' 0 1 2 3 15
    : > "$btmp"; : > "$ctmp"
    for file in "$bdir"/run_*.tsv; do
        [ -f "$file" ] || continue
        awk -F '\t' 'NR > 1 { sum += $20 } END { print sum + 0 }' "$file" >> "$btmp"
    done
    for file in "$cdir"/run_*.tsv; do
        [ -f "$file" ] || continue
        awk -F '\t' 'NR > 1 { sum += $20 } END { print sum + 0 }' "$file" >> "$ctmp"
    done
    bmed=$(median "$btmp"); cmed=$(median "$ctmp")
    ratio=$(awk -v c="$cmed" -v b="$bmed" 'BEGIN { if (b == 0) exit 1; printf "%.6f", c / b }')
    echo "$suite compiler_median_us baseline=$bmed candidate=$cmed ratio=$ratio"

    bmanifest="$bdir/run_0.tsv"; cmanifest="$cdir/run_0.tsv"
    awk -F '\t' 'NR > 1 { print $4 }' "$bmanifest" | sort > "$btmp.ids"
    awk -F '\t' 'NR > 1 { print $4 }' "$cmanifest" | sort > "$ctmp.ids"
    cmp -s "$btmp.ids" "$ctmp.ids" || { echo "$suite sample manifest mismatch" >&2; return 1; }
    dup=$(awk -F '\t' 'NR > 1 { n[$4]++ } END { for (k in n) if (n[k] != 1) bad++ ; print bad + 0 }' "$cmanifest")
    [ "$dup" -eq 0 ] || { echo "$suite duplicate sample IDs" >&2; return 1; }

    # A pre-existing fixture failure may be present in the frozen baseline.
    # Preserve that distinction: a previously passing sample must not regress;
    # a previously failing sample may recover, but not become a new failure.
    awk -F '\t' 'NR == FNR { base[$4] = $6; next }
        NR > 1 && base[$4] == 0 && $6 != 0 { bad++ }
        END { print bad + 0 }' "$bmanifest" "$cmanifest" | {
        read status_regressions
        [ "$status_regressions" -eq 0 ] || {
            echo "$suite new test failures compared with baseline: $status_regressions" >&2
            return 1
        }
    }

    if [ "$suite" = js ]; then
        for kind in complete library; do
            case "$kind" in
                complete)
                    bsum=$(awk -F '\t' 'NR > 1 { s += $25 } END { print s + 0 }' "$bmanifest")
                    csum=$(awk -F '\t' 'NR > 1 { s += $25 } END { print s + 0 }' "$cmanifest")
                    ;;
                library)
                    bsum=$(awk -F '\t' 'NR > 1 && ($5 ~ /^lib_/ || $5 == "underscore_lib") { s += $25 } END { print s + 0 }' "$bmanifest")
                    csum=$(awk -F '\t' 'NR > 1 && ($5 ~ /^lib_/ || $5 == "underscore_lib") { s += $25 } END { print s + 0 }' "$cmanifest")
                    ;;
            esac
            mratio=$(awk -v c="$csum" -v b="$bsum" 'BEGIN { if (b == 0) exit 1; printf "%.6f", c / b }')
            echo "js mir_$kind baseline=$bsum candidate=$csum ratio=$mratio"
            if [ "$kind" = library ]; then
                awk -F '\t' 'NR > 1 && ($5 ~ /^lib_/ || $5 == "underscore_lib") { print $5 "\t" $25 }' "$bmanifest" | sort > "$btmp.lib"
                awk -F '\t' 'NR > 1 && ($5 ~ /^lib_/ || $5 == "underscore_lib") { print $5 "\t" $25 }' "$cmanifest" | sort > "$ctmp.lib"
                join -t '	' "$btmp.lib" "$ctmp.lib" | awk -F '\t' '{ printf "js mir_library_sample %s baseline=%s candidate=%s delta=%s\n", $1, $2, $3, $3-$2 }'
                awk -v r="$mratio" 'BEGIN { if (r > 1.15) print "JS library MIR diagnostic: growth above 15%" > "/dev/stderr" }'
            else
                awk -v r="$mratio" 'BEGIN { if (r > 1.00) print "JS complete MIR diagnostic: volume growth" > "/dev/stderr" }'
            fi
        done
    fi
    if [ "$suite" = lambda ]; then
        awk -v r="$ratio" 'BEGIN { if (r > 0.90) exit 1 }' || { echo "Lambda compiler-time gate failed" >&2; return 1; }
    else
        awk -v r="$ratio" 'BEGIN { if (r > 0.80) exit 1 }' || { echo "JS compiler-time gate failed" >&2; return 1; }
    fi
}

compare_suite lambda
compare_suite js
echo "AST_TUNE_COMPARE PASS"
