# Font Fallback Performance — CJK Bottleneck Fix

> **Status:** Fixed — 3x speedup for CJK text, `wpt-css-text` suite no longer hangs

---

## 1. Problem

Running `make layout suite=wpt-css-text` (1189 tests) would hang indefinitely. Investigation revealed the `text-transform-full-size-kana-*` files (8 files containing CJK kana characters) were **~3x slower** than normal files (186ms vs 10ms per file).

Profiling with macOS `sample` (832 samples) showed **83% of CPU time** in the font fallback path:

```
font_load_glyph                               690/832 samples (83%)
  └─ font_find_codepoint_fallback             669 samples
       └─ font_database_find_best_match_internal  470 samples
            ├─ calculate_match_score           ~300 samples
            └─ organize_fonts_into_families    101 samples
```

Each CJK codepoint not in the primary font triggers `font_find_codepoint_fallback()`, which iterates all 13 fallback font names and calls `font_database_find_best_match_internal()` for each.

---

## 2. Root Cause Analysis

Three compounding inefficiencies in `lib/font/font_database.c`:

### 2.1. `organize_fonts_into_families()` — Full Re-index Every Call

```c
// BEFORE: iterates ALL fonts from index 0 every time
static void organize_fonts_into_families(FontDatabase* db) {
    for (int i = 0; i < db->all_fonts->length; i++) {
        FontEntry* entry = (FontEntry*)db->all_fonts->data[i];
        ...
        arraylist_append(family->fonts, entry);
    }
}
```

This function was called after every lazy-parse cycle. With ~500 fonts in `all_fonts`, each call:
- Re-iterated all 500 entries (even those already organized)
- **Appended duplicate entries** to family font lists (no dedup check)
- O(n) work per call, called O(fallback_fonts × unique_codepoints) times

### 2.2. Unconditional Organize After Lazy-Parse

```c
// BEFORE: always re-organizes, even if nothing was parsed
if (!family) {
    for (int i = 0; i < db->all_fonts->length; i++) {
        // try to parse matching placeholders...
    }
    organize_fonts_into_families(db);  // called even if 0 placeholders matched
    family = hashmap_get(db->families, &search_fam);
}
```

On macOS, many fallback families (e.g., "Noto Color Emoji", "Liberation Sans", "Nimbus Sans") don't exist as installed fonts. Every lookup for these names triggered the full:
1. Scan all `all_fonts` for placeholder matches (found none)
2. Call `organize_fonts_into_families` (processing all 500 fonts again for nothing)
3. Still find no family
4. Fall through to "try all fonts" — score every non-placeholder font

### 2.3. "Try All Fonts" Fallback — O(all_fonts) Scoring

```c
// BEFORE: if family not found, score EVERY font in the database
if (!best_font) {
    for (int i = 0; i < db->all_fonts->length; i++) {
        FontEntry* e = (FontEntry*)db->all_fonts->data[i];
        if (!e || e->is_placeholder) continue;
        float score = calculate_match_score(e, criteria);  // expensive
        ...
    }
}
```

`calculate_match_score()` is expensive per call:
- `str_ieq()` for family name matching
- Generic family table lookup (nested loops)
- `strrchr()` + `strstr()` for filename analysis
- `snprintf()` to build expected filename string
- Walk `unicode_ranges` linked list for codepoint check

With ~500 fonts × 13 fallback names × 63 unique CJK codepoints = **~400,000 score calculations**.

### 2.4. Combined Impact

For a single CJK glyph miss:

```
font_find_codepoint_fallback(U+30A2)
  ├─ "Noto Color Emoji" → find_best_match(criteria)
  │    ├─ scan all_fonts for placeholder (500 entries, 0 match)
  │    ├─ organize_fonts_into_families (500 entries, duplicates appended)
  │    ├─ family not found
  │    └─ score ALL fonts (500 × calculate_match_score)
  ├─ "Apple Color Emoji" → ... (same cycle)
  ├─ "Segoe UI Emoji" → ... (same cycle)
  ├─ "Liberation Sans" → ... (same cycle, not on macOS)
  ├─ "DejaVu Sans" → ... (same cycle, not on macOS)
  ├─ ... (13 fallback fonts total, ~6 missing on macOS)
  └─ platform lookup (CoreText) → finds the font
```

Each missing family: **500 placeholder scans + 500 organize iterations + 500 score calculations** = 1500 operations of wasted work, repeated for every missing font family on every unique codepoint.

---

## 3. Fix

Three targeted changes in `lib/font/font_database.c` and `lib/font/font_internal.h`:

### 3.1. Incremental `organize_fonts_into_families()`

Track how far we've organized and only process new entries:

```c
// FontDatabase struct — new fields (font_internal.h)
typedef struct FontDatabase {
    ...
    int         organized_up_to;        // index in all_fonts up to which families are organized
    HashMap*    missing_families;       // family names confirmed absent after lazy parsing
    ...
} FontDatabase;
```

```c
// AFTER: only process entries added since last organize
static void organize_fonts_into_families(FontDatabase* db) {
    int start = db->organized_up_to;
    for (int i = start; i < db->all_fonts->length; i++) {
        FontEntry* entry = (FontEntry*)db->all_fonts->data[i];
        if (!entry || !entry->family_name) continue;

        FontFamily search = {.family_name = entry->family_name};
        FontFamily* family = (FontFamily*)hashmap_get(db->families, &search);

        if (!family) {
            FontFamily new_fam = {0};
            new_fam.family_name = entry->family_name;
            new_fam.fonts = arraylist_new(0);
            new_fam.is_system_family = true;
            hashmap_set(db->families, &new_fam);
            family = (FontFamily*)hashmap_get(db->families, &search);

            // newly discovered family — remove from missing cache if present
            if (db->missing_families) {
                hashmap_delete(db->missing_families, &search);
            }
        }

        if (family && family->fonts) {
            arraylist_append(family->fonts, entry);
        }
        // ... index by postscript_name, file_path ...
    }
    db->organized_up_to = db->all_fonts->length;
}
```

**Effect:** After initial scan organizes all 500 fonts, subsequent calls process 0 entries (unless new fonts were lazy-parsed). Eliminates duplicate accumulation.

### 3.2. Missing Family Cache

Cache family names that are confirmed absent. Future lookups return empty immediately:

```c
// font_database_find_best_match_internal — new fast path
FontFamily search_fam = {.family_name = criteria->family_name};
if (db->missing_families && hashmap_get(db->missing_families, &search_fam)) {
    return result; // confirmed absent — skip all work
}
```

When lazy-parse finds no matching placeholders and the family doesn't exist:

```c
if (!family) {
    FontFamily missing = {.family_name = criteria->family_name};
    hashmap_set(db->missing_families, &missing);
    return result;  // early return, no "try all fonts" fallback
}
```

**Effect:** Each missing family is looked up once, then O(1) rejection forever. The 6+ missing families on macOS (Noto Color Emoji, Segoe UI Emoji, Liberation Sans, DejaVu Sans, Liberation Serif, Nimbus Sans) become instant no-ops.

### 3.3. Conditional Organize

Only call `organize_fonts_into_families` when placeholders were actually parsed:

```c
if (!family) {
    bool parsed_any = false;
    for (int i = 0; i < db->all_fonts->length; i++) {
        FontEntry* e = (FontEntry*)db->all_fonts->data[i];
        if (!e || !e->is_placeholder || !e->family_name) continue;
        if (str_ieq(e->family_name, ..., criteria->family_name, ...)) {
            parse_placeholder_font(e, db->arena); // or parse_ttc_font_metadata
            parsed_any = true;
        }
    }
    if (parsed_any) {
        organize_fonts_into_families(db);
    }
    family = hashmap_get(db->families, &search_fam);
}
```

**Effect:** When no placeholders match (common for missing families), skips the organize call entirely.

---

## 4. Files Changed

| File | Change |
|------|--------|
| `lib/font/font_internal.h` | Added `organized_up_to` (int) and `missing_families` (HashMap*) to `FontDatabase` struct |
| `lib/font/font_database.c` | Incremental organize, missing family cache, conditional organize, removed "try all fonts" fallback |

---

## 5. Performance Results

### Per-file timing (debug build, macOS, `text-transform-full-size-kana-*`)

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| Avg per file | ~186ms | ~64ms | **2.9x faster** |
| 8 files total | ~1.5s | ~0.5s | **3x faster** |

### Suite completion

| Suite | Before | After |
|-------|--------|-------|
| `wpt-css-text` (1189 tests) | **Hangs indefinitely** | Completes (241 pass, 948 fail, 7 skipped) |
| `page` (39 tests) | No regression | No regression |
| `test-radiant-baseline` (32 tests) | 32/32 pass | 32/32 pass |

---

## 6. Call Flow After Fix

```
font_find_codepoint_fallback(U+30A2)
  ├─ check codepoint_fallback_cache → miss (first time)
  ├─ "Noto Color Emoji" → find_best_match
  │    └─ missing_families cache hit → return empty (O(1))
  ├─ "Apple Color Emoji" → find_best_match
  │    └─ families hashmap hit → score 2-3 fonts → found
  │    └─ font_load_face → font_has_codepoint? no
  ├─ "Liberation Sans" → missing_families cache hit → O(1)
  ├─ "DejaVu Sans" → missing_families cache hit → O(1)
  ├─ ... (remaining missing families: all O(1))
  └─ platform lookup (CoreText) → finds Hiragino Sans
       └─ cache positive result
```

Second lookup for same codepoint:
```
font_find_codepoint_fallback(U+30A2)
  └─ codepoint_fallback_cache hit → return cached handle (O(1))
```
