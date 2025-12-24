# HTML5 Parser Improvements: Learning from Gumbo

**Date**: December 24, 2025
**Status**: Implemented (Phase 1 Complete)
**Priority**: High

## Overview

This document outlines improvements to Lambda's HTML5 parser based on analysis of Google's Gumbo parser. The goal is to achieve better HTML5 spec compliance without the complexity of Gumbo's 33K+ lines of code.

## Comparison Summary

| Feature | Gumbo | Lambda Current | Gap |
|---------|-------|----------------|-----|
| Named entities | 2,241 | 247 | 1,994 missing |
| SVG attribute replacements | 50 | 55 ✅ | Implemented |
| SVG tag replacements | 34 | 37 ✅ | Implemented |
| Foreign namespace attrs | 10 | 10 ✅ | Implemented |
| Invalid codepoint replacements | 35 | 32 ✅ | Already existed |
| Quirks mode patterns | 40+ | 1 | Low |

## Implemented Changes (Phase 1)

### Files Modified

1. **`lambda/input/html5/html5_parser.cpp`**:
   - Added SVG attribute replacement table (55 entries)
   - Added SVG tag replacement table (37 entries)
   - Added foreign namespace attribute table (10 entries)
   - Added `html5_lookup_svg_tag()` function
   - Added `html5_lookup_svg_attr()` function
   - Added `html5_is_in_svg_namespace()` function
   - Modified `html5_create_element_for_token()` to apply corrections

### Test Results

```
./test/test_html_gtest.exe: 49/49 tests PASSED
SVG parsing: clippath → clipPath ✅
SVG attributes: viewbox → viewBox ✅
```

---

## Implementation Plan

### Phase 1: SVG/MathML Namespace Support (Critical)

**Problem**: SVG documents parsed through HTML5 require case-sensitive attribute and tag names. HTML5 tokenizer lowercases everything, so the tree builder must restore correct casing.

**Files to modify**:
- `lambda/input/html5/html5_tree_builder.cpp`

**Implementation**:

#### 1.1 SVG Attribute Replacements

Add lookup table for SVG attributes that need camelCase restoration:

```cpp
static const char* svg_attribute_replacements[][2] = {
    {"attributename", "attributeName"},
    {"attributetype", "attributeType"},
    {"basefrequency", "baseFrequency"},
    {"baseprofile", "baseProfile"},
    {"calcmode", "calcMode"},
    {"clippathunits", "clipPathUnits"},
    {"diffuseconstant", "diffuseConstant"},
    {"edgemode", "edgeMode"},
    {"filterunits", "filterUnits"},
    {"glyphref", "glyphRef"},
    {"gradienttransform", "gradientTransform"},
    {"gradientunits", "gradientUnits"},
    {"kernelmatrix", "kernelMatrix"},
    {"kernelunitlength", "kernelUnitLength"},
    {"keypoints", "keyPoints"},
    {"keysplines", "keySplines"},
    {"keytimes", "keyTimes"},
    {"lengthadjust", "lengthAdjust"},
    {"limitingconeangle", "limitingConeAngle"},
    {"markerheight", "markerHeight"},
    {"markerunits", "markerUnits"},
    {"markerwidth", "markerWidth"},
    {"maskcontentunits", "maskContentUnits"},
    {"maskunits", "maskUnits"},
    {"numoctaves", "numOctaves"},
    {"pathlength", "pathLength"},
    {"patterncontentunits", "patternContentUnits"},
    {"patterntransform", "patternTransform"},
    {"patternunits", "patternUnits"},
    {"pointsatx", "pointsAtX"},
    {"pointsaty", "pointsAtY"},
    {"pointsatz", "pointsAtZ"},
    {"preservealpha", "preserveAlpha"},
    {"preserveaspectratio", "preserveAspectRatio"},
    {"primitiveunits", "primitiveUnits"},
    {"refx", "refX"},
    {"refy", "refY"},
    {"repeatcount", "repeatCount"},
    {"repeatdur", "repeatDur"},
    {"requiredextensions", "requiredExtensions"},
    {"requiredfeatures", "requiredFeatures"},
    {"specularconstant", "specularConstant"},
    {"specularexponent", "specularExponent"},
    {"spreadmethod", "spreadMethod"},
    {"startoffset", "startOffset"},
    {"stddeviation", "stdDeviation"},
    {"stitchtiles", "stitchTiles"},
    {"surfacescale", "surfaceScale"},
    {"systemlanguage", "systemLanguage"},
    {"tablevalues", "tableValues"},
    {"targetx", "targetX"},
    {"targety", "targetY"},
    {"textlength", "textLength"},
    {"viewbox", "viewBox"},
    {"viewtarget", "viewTarget"},
    {"xchannelselector", "xChannelSelector"},
    {"ychannelselector", "yChannelSelector"},
    {"zoomandpan", "zoomAndPan"},
    {nullptr, nullptr}
};
```

#### 1.2 SVG Tag Replacements

Add lookup table for SVG tags that need camelCase restoration:

```cpp
static const char* svg_tag_replacements[][2] = {
    {"altglyph", "altGlyph"},
    {"altglyphdef", "altGlyphDef"},
    {"altglyphitem", "altGlyphItem"},
    {"animatecolor", "animateColor"},
    {"animatemotion", "animateMotion"},
    {"animatetransform", "animateTransform"},
    {"clippath", "clipPath"},
    {"feblend", "feBlend"},
    {"fecolormatrix", "feColorMatrix"},
    {"fecomponenttransfer", "feComponentTransfer"},
    {"fecomposite", "feComposite"},
    {"feconvolvematrix", "feConvolveMatrix"},
    {"fediffuselighting", "feDiffuseLighting"},
    {"fedisplacementmap", "feDisplacementMap"},
    {"fedistantlight", "feDistantLight"},
    {"fedropshadow", "feDropShadow"},
    {"feflood", "feFlood"},
    {"fefunca", "feFuncA"},
    {"fefuncb", "feFuncB"},
    {"fefuncg", "feFuncG"},
    {"fefuncr", "feFuncR"},
    {"fegaussianblur", "feGaussianBlur"},
    {"feimage", "feImage"},
    {"femerge", "feMerge"},
    {"femergenode", "feMergeNode"},
    {"femorphology", "feMorphology"},
    {"feoffset", "feOffset"},
    {"fepointlight", "fePointLight"},
    {"fespecularlighting", "feSpecularLighting"},
    {"fespotlight", "feSpotLight"},
    {"fetile", "feTile"},
    {"feturbulence", "feTurbulence"},
    {"foreignobject", "foreignObject"},
    {"glyphref", "glyphRef"},
    {"lineargradient", "linearGradient"},
    {"radialgradient", "radialGradient"},
    {"textpath", "textPath"},
    {nullptr, nullptr}
};
```

#### 1.3 Foreign Namespace Attribute Handling

For attributes like `xlink:href`, `xml:lang`, etc., we need to preserve namespace prefixes:

```cpp
// Foreign attributes that need namespace handling
static const char* foreign_attributes[][2] = {
    {"xlink:actuate", "xlink:actuate"},
    {"xlink:arcrole", "xlink:arcrole"},
    {"xlink:href", "xlink:href"},
    {"xlink:role", "xlink:role"},
    {"xlink:show", "xlink:show"},
    {"xlink:title", "xlink:title"},
    {"xlink:type", "xlink:type"},
    {"xml:base", "xml:base"},
    {"xml:lang", "xml:lang"},
    {"xml:space", "xml:space"},
    {"xmlns", "xmlns"},
    {"xmlns:xlink", "xmlns:xlink"},
    {nullptr, nullptr}
};
```

### Phase 2: Invalid Codepoint Replacement (Medium Priority)

**Problem**: Windows-1252 characters (0x80-0x9F) are commonly misused in legacy HTML. The HTML5 spec requires specific replacements.

**Files to modify**:
- `lambda/input/html5/html5_tokenizer.cpp`

**Implementation**:

```cpp
// Windows-1252 character replacement table (per HTML5 spec)
static const int codepoint_replacements[][2] = {
    {0x00, 0xFFFD},  // NULL -> REPLACEMENT CHARACTER
    {0x0D, 0x000D},  // CR stays CR
    {0x80, 0x20AC},  // € (Euro sign)
    {0x81, 0x0081},  // (undefined, keep)
    {0x82, 0x201A},  // ‚ (Single Low-9 Quote)
    {0x83, 0x0192},  // ƒ (Latin Small F with Hook)
    {0x84, 0x201E},  // „ (Double Low-9 Quote)
    {0x85, 0x2026},  // … (Horizontal Ellipsis)
    {0x86, 0x2020},  // † (Dagger)
    {0x87, 0x2021},  // ‡ (Double Dagger)
    {0x88, 0x02C6},  // ˆ (Modifier Letter Circumflex)
    {0x89, 0x2030},  // ‰ (Per Mille Sign)
    {0x8A, 0x0160},  // Š (Latin Capital S with Caron)
    {0x8B, 0x2039},  // ‹ (Single Left Angle Quote)
    {0x8C, 0x0152},  // Œ (Latin Capital Ligature OE)
    {0x8D, 0x008D},  // (undefined, keep)
    {0x8E, 0x017D},  // Ž (Latin Capital Z with Caron)
    {0x8F, 0x008F},  // (undefined, keep)
    {0x90, 0x0090},  // (undefined, keep)
    {0x91, 0x2018},  // ' (Left Single Quote)
    {0x92, 0x2019},  // ' (Right Single Quote)
    {0x93, 0x201C},  // " (Left Double Quote)
    {0x94, 0x201D},  // " (Right Double Quote)
    {0x95, 0x2022},  // • (Bullet)
    {0x96, 0x2013},  // – (En Dash)
    {0x97, 0x2014},  // — (Em Dash)
    {0x98, 0x02DC},  // ˜ (Small Tilde)
    {0x99, 0x2122},  // ™ (Trade Mark)
    {0x9A, 0x0161},  // š (Latin Small S with Caron)
    {0x9B, 0x203A},  // › (Single Right Angle Quote)
    {0x9C, 0x0153},  // œ (Latin Small Ligature OE)
    {0x9D, 0x009D},  // (undefined, keep)
    {0x9E, 0x017E},  // ž (Latin Small Z with Caron)
    {0x9F, 0x0178},  // Ÿ (Latin Capital Y with Diaeresis)
    {-1, -1}  // terminator
};
```

### Phase 3: Extended Named Entity Table (Medium Priority)

**Problem**: Lambda has 247 named entities; HTML5 spec defines 2,241. Missing entities cause `&EntityName;` to appear literally.

**Approach**: Generate a complete entity table from WHATWG spec. Two options:

1. **Option A**: Static table with hash lookup (recommended)
   - Generate `html5_entities.h` with ~2200 entries
   - Use perfect hash (gperf) or simple hash with fallback
   - ~50KB additional binary size

2. **Option B**: Trie-based lookup
   - More complex but faster for partial matches
   - Better for streaming/incremental parsing

**Decision**: Option A (static table) - simpler, good enough performance for our use case.

### Phase 4: Quirks Mode Detection (Low Priority)

**Problem**: Lambda only checks `<!DOCTYPE html>` for standards mode. Legacy DOCTYPEs should trigger quirks mode.

**Implementation**: Add legacy DOCTYPE pattern matching in `html5_process_in_initial_mode()`:

```cpp
// Quirks mode public ID prefixes (simplified list)
static const char* quirks_prefixes[] = {
    "-//W3O//DTD W3 HTML Strict 3.0//",
    "-//W3C//DTD HTML 4.0 Transitional//",
    "-//W3C//DTD HTML 4.0 Frameset//",
    "-//IETF//DTD HTML//",
    "-//Netscape Comm. Corp.//DTD HTML//",
    "-//Microsoft//DTD Internet Explorer",
    // ... more patterns
    nullptr
};
```

## Testing Strategy

1. **WPT Tests**: Run existing html5lib-tests (tree construction)
2. **SVG Tests**: Create test cases for SVG attribute/tag casing
3. **Entity Tests**: Test common and obscure entities
4. **Regression**: Ensure existing tests pass

## Implementation Order

1. ✅ Phase 1.1: SVG attribute replacements (55 entries)
2. ✅ Phase 1.2: SVG tag replacements (37 entries)
3. ✅ Phase 1.3: Foreign namespace attributes (10 entries)
4. ✅ Phase 2: Codepoint replacements (already existed - 32 entries)
5. ✅ Phase 3: Extended entity table (2,125 entries - complete WHATWG spec)
6. ✅ Phase 4: Quirks mode detection (57 legacy patterns + full DOCTYPE parsing)

## Files Modified

- `lambda/input/html5/html5_tree_builder.cpp` - SVG/MathML handling, quirks mode detection
- `lambda/input/html5/html5_tokenizer.cpp` - Entity table (2,125 entries), DOCTYPE PUBLIC/SYSTEM parsing
- `lambda/input/html5/html5_parser.h` - New constants/declarations

## Phase 3 Implementation Details (Entity Table)

**Generator script**: `utils/generate_html5_entities.py`
- Fetches WHATWG entities.json (2,125 named character references)
- Generates UTF-8 encoded C table sorted alphabetically
- Output: `temp/html5_entities.inc`

**Changes**:
- Entity table: 247 → 2,125 entries (complete WHATWG spec coverage)
- Lookup: O(n) linear → O(log n) binary search
- File size increase: ~1,900 lines added

## Phase 4 Implementation Details (Quirks Mode)

**Tokenizer enhancements**:
- Full DOCTYPE PUBLIC/SYSTEM identifier parsing
- 10 new tokenizer states implemented
- Public/System IDs stored in token and DOM

**Quirks detection** (per WHATWG spec 13.2.6.4.1):
- 57 legacy public ID prefixes for quirks mode
- 3 exact public ID matches for quirks mode
- 1 system ID match (IBM XHTML)
- 2 prefixes that trigger quirks only when system ID missing
- 4 prefixes for limited quirks mode

**Quirks mode states**:
- `parser->quirks_mode` - full quirks mode
- `parser->limited_quirks_mode` - limited quirks mode

## References

- [WHATWG HTML5 Parsing Spec](https://html.spec.whatwg.org/multipage/parsing.html)
- [Named Character References](https://html.spec.whatwg.org/multipage/named-characters.html)
- [Gumbo Parser Source](https://github.com/nickg/gumbo-parser)
