#!/bin/bash
# Rename math test fixtures to follow group_name_ prefix convention
# Run from: test/latex/fixtures/math/

set -e

echo "Renaming math test fixtures to follow group_name_ prefix convention..."
echo ""

# Function to rename a file with git mv (preserves history)
rename_file() {
    local old="$1"
    local new="$2"
    if [ -f "$old" ] && [ "$old" != "$new" ]; then
        echo "  $old -> $new"
        git mv "$old" "$new" 2>/dev/null || mv "$old" "$new"
    fi
}

# ============================================================================
# TOP-LEVEL FILES
# ============================================================================
echo "=== Renaming top-level files ==="

# accents_ group
rename_file "accents.tex" "accents_basic.tex"
rename_file "testover.tex" "accents_over.tex"

# arrays_ group (matrices, environments)
rename_file "array.tex" "arrays_basic.tex"
rename_file "test_matrix.tex" "arrays_matrix.tex"

# bigops_ group
rename_file "big_operators.tex" "bigops_basic.tex"
rename_file "test_sum_integral.tex" "bigops_sum_integral.tex"

# delims_ group
rename_file "extensible_delims.tex" "delims_extensible.tex"
rename_file "test_delimiters.tex" "delims_basic.tex"

# fonts_ group
rename_file "test_font_styles.tex" "fonts_styles.tex"

# fracs_ group
rename_file "fracs.tex" "fracs_basic.tex"
rename_file "test_fraction.tex" "fracs_test.tex"
rename_file "choose.tex" "fracs_choose.tex"

# greek_ group
rename_file "test_greek.tex" "greek_basic.tex"
rename_file "test_all_greek.tex" "greek_all.tex"

# negation_ group
rename_file "not.tex" "negation_basic.tex"

# operators_ group
rename_file "test_all_operators.tex" "operators_all.tex"
rename_file "ambiguous_relations.tex" "operators_ambiguous_relations.tex"

# radicals_ group
rename_file "test_sqrt.tex" "radicals_sqrt.tex"

# scripts_ group
rename_file "test_subscript_superscript.tex" "scripts_basic.tex"
rename_file "testscripts.tex" "scripts_test.tex"

# spacing_ group
rename_file "phantoms.tex" "spacing_phantoms.tex"

# symbols_ group
rename_file "arrows.tex" "symbols_arrows.tex"

# nested_ group
rename_file "test_nested1.tex" "nested_test1.tex"
rename_file "test_nested2.tex" "nested_test2.tex"
rename_file "test_nested_structures.tex" "nested_structures.tex"
rename_file "test_complex_formula.tex" "nested_complex.tex"

# misc_ group
rename_file "sampler.tex" "misc_sampler.tex"
rename_file "simplemath.tex" "misc_simplemath.tex"
rename_file "compact_dual.tex" "misc_compact_dual.tex"
rename_file "declare.tex" "misc_declare.tex"

echo ""

# ============================================================================
# MATHLIVE SUBDIRECTORY
# ============================================================================
echo "=== Renaming mathlive/ files ==="

cd mathlive 2>/dev/null || { echo "mathlive/ directory not found"; exit 1; }

# accents_ group (already correct prefix)
# No changes needed for accents_*.tex

# overunder -> accents_overunder
for f in overunder_*.tex; do
    [ -f "$f" ] && rename_file "$f" "accents_${f}"
done

# boxes_ group (already correct prefix)
# No changes needed for boxes_*.tex

# rule_and_dimensions -> boxes_rule
for f in rule_and_dimensions_*.tex; do
    [ -f "$f" ] && rename_file "$f" "boxes_rule_${f#rule_and_dimensions_}"
done

# delimiter_sizing -> delims_sizing
for f in delimiter_sizing_*.tex; do
    [ -f "$f" ] && rename_file "$f" "delims_sizing_${f#delimiter_sizing_}"
done

# left_right -> delims_leftright
for f in left_right_*.tex; do
    [ -f "$f" ] && rename_file "$f" "delims_leftright_${f#left_right_}"
done

# environments -> arrays_env
for f in environments_*.tex; do
    [ -f "$f" ] && rename_file "$f" "arrays_env_${f#environments_}"
done

# fonts_ group (already correct prefix)
# No changes needed for fonts_*.tex

# mode_shift -> fonts_mode
for f in mode_shift_*.tex; do
    [ -f "$f" ] && rename_file "$f" "fonts_mode_${f#mode_shift_}"
done

# fractions -> fracs
for f in fractions_*.tex; do
    [ -f "$f" ] && rename_file "$f" "fracs_${f#fractions_}"
done

# negation_ group (already correct prefix)
# No changes needed for negation_*.tex

# operators_ group (already correct prefix)
# No changes needed for operators_*.tex

# ordinary_symbols -> symbols_ordinary
for f in ordinary_symbols_*.tex; do
    [ -f "$f" ] && rename_file "$f" "symbols_ordinary_${f#ordinary_symbols_}"
done

# phantom -> spacing_phantom
for f in phantom_*.tex; do
    [ -f "$f" ] && rename_file "$f" "spacing_phantom_${f#phantom_}"
done

# radicals_ group (already correct prefix)
# No changes needed for radicals_*.tex

# sizing -> styles_sizing
for f in sizing_*.tex; do
    [ -f "$f" ] && rename_file "$f" "styles_sizing_${f#sizing_}"
done

# spacing_ group (already correct prefix)
# No changes needed for spacing_*.tex

# styles_ group (already correct prefix)
# No changes needed for styles_*.tex

# styling -> styles_styling
for f in styling_*.tex; do
    [ -f "$f" ] && rename_file "$f" "styles_${f}"
done

# extensions -> misc_extensions
for f in extensions_*.tex; do
    [ -f "$f" ] && rename_file "$f" "misc_extensions_${f#extensions_}"
done

cd ..
echo ""

# ============================================================================
# SUBJECTS SUBDIRECTORY
# ============================================================================
echo "=== Renaming subjects/ files ==="

cd subjects 2>/dev/null || { echo "subjects/ directory not found"; exit 1; }

# Rename test_* to subjects_*
for f in test_*.tex; do
    [ -f "$f" ] && rename_file "$f" "subjects_${f#test_}"
done

cd ..
echo ""

echo "=== Done ==="
echo ""
echo "Summary of new prefixes:"
echo "  accents_    - Accents, decorations, over/under"
echo "  arrays_     - Arrays, matrices, environments"
echo "  bigops_     - Big operators (sum, int, prod)"
echo "  boxes_      - Boxes, rules, dimensions"
echo "  delims_     - Delimiters, left/right, sizing"
echo "  fonts_      - Font styles, mode shifts"
echo "  fracs_      - Fractions, binomials, choose"
echo "  greek_      - Greek letters"
echo "  misc_       - Miscellaneous"
echo "  negation_   - Negation, not"
echo "  nested_     - Nested/complex structures"
echo "  operators_  - Operators, relations"
echo "  radicals_   - Square roots, nth roots"
echo "  scripts_    - Subscripts, superscripts"
echo "  spacing_    - Spacing, phantoms"
echo "  styles_     - Display styles, sizing"
echo "  subjects_   - Real-world subject tests"
echo "  symbols_    - Ordinary symbols, arrows"
