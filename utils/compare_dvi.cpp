// compare_dvi.cpp - Compare two DVI files and report differences
//
// Usage: compare_dvi reference.dvi output.dvi

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern "C" {
#include "../lib/arena.h"
#include "../lib/mempool.h"
}
#include "../lambda/tex/dvi_parser.hpp"

using namespace tex::dvi;

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s reference.dvi output.dvi\n", argv[0]);
        return 1;
    }

    const char* ref_file = argv[1];
    const char* out_file = argv[2];

    // Create arenas
    Pool* pool = pool_create();
    Arena* arena1 = arena_create_default(pool);
    Arena* arena2 = arena_create_default(pool);

    // Parse both files
    DVIParser ref_parser(arena1);
    DVIParser out_parser(arena2);

    printf("Parsing reference: %s\n", ref_file);
    if (!ref_parser.parse_file(ref_file)) {
        fprintf(stderr, "Failed to parse reference DVI file: %s\n", ref_file);
        return 1;
    }

    printf("Parsing output: %s\n", out_file);
    if (!out_parser.parse_file(out_file)) {
        fprintf(stderr, "Failed to parse output DVI file: %s\n", out_file);
        return 1;
    }

    // Compare page counts
    int ref_pages = ref_parser.page_count();
    int out_pages = out_parser.page_count();

    printf("\nPage count: reference=%d, output=%d\n", ref_pages, out_pages);
    if (ref_pages != out_pages) {
        printf("ERROR: Page count mismatch!\n");
        return 1;
    }

    // Compare each page
    bool all_match = true;
    for (int i = 0; i < ref_pages; i++) {
        const DVIPage* ref_page = ref_parser.page(i);
        const DVIPage* out_page = out_parser.page(i);

        printf("\nPage %d:\n", i + 1);
        printf("  Reference: %d glyphs\n", ref_page->glyph_count);
        printf("  Output:    %d glyphs\n", out_page->glyph_count);

        if (ref_page->glyph_count != out_page->glyph_count) {
            printf("  ERROR: Glyph count mismatch!\n");
            all_match = false;
            continue;
        }

        // Compare glyphs
        int mismatches = 0;
        for (int j = 0; j < ref_page->glyph_count; j++) {
            const DVIGlyph& ref_g = ref_page->glyphs[j];
            const DVIGlyph& out_g = out_page->glyphs[j];

            bool match = (ref_g.character == out_g.character &&
                         ref_g.font == out_g.font &&
                         ref_g.h == out_g.h &&
                         ref_g.v == out_g.v);

            if (!match) {
                if (mismatches < 10) {  // Only show first 10 mismatches
                    printf("  Glyph %d mismatch:\n", j);
                    printf("    Ref: char=%3d font=%d h=%7d v=%7d\n",
                           ref_g.character, ref_g.font, ref_g.h, ref_g.v);
                    printf("    Out: char=%3d font=%d h=%7d v=%7d\n",
                           out_g.character, out_g.font, out_g.h, out_g.v);
                }
                mismatches++;
            }
        }

        if (mismatches > 0) {
            printf("  ERROR: %d glyph mismatches (showing first 10)\n", mismatches);
            all_match = false;
        } else {
            printf("  ✓ All glyphs match\n");
        }
    }

    // Cleanup
    arena_destroy(arena1);
    arena_destroy(arena2);
    pool_destroy(pool);

    if (all_match) {
        printf("\n✓ DVI files match!\n");
        return 0;
    } else {
        printf("\n✗ DVI files differ\n");
        return 1;
    }
}
