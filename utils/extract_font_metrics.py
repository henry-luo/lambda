#!/usr/bin/env python3
"""
extract_font_metrics.py — Extract font metrics from TTF files for Redex layout engine.

Uses fontTools to extract per-glyph advance widths, kerning pairs, and vertical
metrics from TrueType font files. Outputs one JSON file per font face.

Usage:
    python3 utils/extract_font_metrics.py                     # extract all test fonts
    python3 utils/extract_font_metrics.py --font path/to.ttf  # extract one font
    python3 utils/extract_font_metrics.py --list               # list available fonts

Output: test/redex/font-metrics/<FontName>-<Style>.json

The JSON structure matches what the Racket layout engine expects:
{
    "font_name": "LiberationSerif",
    "font_style": "Regular",
    "units_per_em": 2048,
    "hhea_ascender": 1825,
    "hhea_descender": -443,
    "hhea_line_gap": 0,
    "typo_ascender": 1420,
    "typo_descender": -442,
    "typo_line_gap": 307,
    "win_ascent": 1825,
    "win_descent": 443,
    "x_height": 1082,
    "cap_height": 1409,
    "space_width": 512,
    "line_height_ratio": 1.1083984375,
    "ascender_ratio": 0.89111328125,
    "descender_ratio": 0.216308593750,
    "glyph_widths": {
        "32": 512,
        "33": 682,
        ...
    },
    "char_width_ratios": {
        "32": 0.25,
        "33": 0.333,
        ...
    },
    "kern_pairs": {
        "65,86": -140,
        ...
    }
}
"""

import json
import os
import sys
import argparse
from pathlib import Path

try:
    from fontTools.ttLib import TTFont
except ImportError:
    print("ERROR: fontTools not installed. Run: pip3 install fonttools", file=sys.stderr)
    sys.exit(1)


# Default font directory and output directory
FONT_DIR = Path(__file__).parent.parent / "test" / "layout" / "data" / "font"
OUTPUT_DIR = Path(__file__).parent.parent / "test" / "redex" / "font-metrics"

# Chrome-compatible normal line-height calculation:
# Chrome uses: hhea.ascender + |hhea.descender| + hhea.lineGap (if OS/2 fsSelection USE_TYPO_METRICS not set)
# or: OS/2.sTypoAscender + |OS/2.sTypoDescender| + OS/2.sTypoLineGap (if USE_TYPO_METRICS set)
# For most fonts: max(win_ascent + win_descent, hhea_ascender + |hhea_descender| + hhea_line_gap)
# The actual Chrome algorithm varies by platform; we use the hhea approach (matches macOS Chrome).


def extract_metrics(font_path: str) -> dict:
    """Extract comprehensive font metrics from a TTF file."""
    font = TTFont(font_path)

    # Basic info
    name_table = font.get("name")
    font_name = _get_name(name_table, 1) or Path(font_path).stem  # family name
    font_style = _get_name(name_table, 2) or "Regular"  # subfamily (style)

    # Units per em — all glyph coordinates are in this unit
    head = font["head"]
    units_per_em = head.unitsPerEm

    # Vertical metrics from hhea table (horizontal header)
    hhea = font["hhea"]
    hhea_ascender = hhea.ascent
    hhea_descender = hhea.descent  # negative value
    hhea_line_gap = hhea.lineGap

    # OS/2 table vertical metrics (alternative source)
    os2 = font.get("OS/2")
    typo_ascender = os2.sTypoAscender if os2 else hhea_ascender
    typo_descender = os2.sTypoDescender if os2 else hhea_descender
    typo_line_gap = os2.sTypoLineGap if os2 else hhea_line_gap
    win_ascent = os2.usWinAscent if os2 else hhea_ascender
    win_descent = os2.usWinDescent if os2 else abs(hhea_descender)

    # x-height and cap-height from OS/2 (if available)
    x_height = os2.sxHeight if (os2 and hasattr(os2, 'sxHeight')) else 0
    cap_height = os2.sCapHeight if (os2 and hasattr(os2, 'sCapHeight')) else 0

    # Check USE_TYPO_METRICS flag (bit 7 of fsSelection)
    use_typo_metrics = bool(os2 and (os2.fsSelection & (1 << 7)))

    # Chrome line-height calculation (macOS):
    # if USE_TYPO_METRICS: typo_ascender - typo_descender + typo_line_gap
    # else: max(win_ascent + win_descent, hhea_ascender - hhea_descender + hhea_line_gap)
    if use_typo_metrics:
        chrome_line_height_units = typo_ascender - typo_descender + typo_line_gap
    else:
        hhea_total = hhea_ascender - hhea_descender + hhea_line_gap
        win_total = win_ascent + win_descent
        chrome_line_height_units = max(hhea_total, win_total)

    # Derived ratios (relative to font-size = 1.0)
    line_height_ratio = chrome_line_height_units / units_per_em
    ascender_ratio = hhea_ascender / units_per_em
    descender_ratio = abs(hhea_descender) / units_per_em

    # Per-glyph advance widths from hmtx table
    hmtx = font["hmtx"]
    cmap = font.getBestCmap()  # Unicode → glyph name mapping

    glyph_widths = {}       # codepoint (int) → advance width (font units)
    char_width_ratios = {}  # codepoint (int) → width ratio (0.0-1.0+)

    if cmap:
        for codepoint, glyph_name in sorted(cmap.items()):
            # Skip control characters and very high codepoints
            if codepoint > 0xFFFF:
                continue
            advance_width, _ = hmtx[glyph_name]
            glyph_widths[str(codepoint)] = advance_width
            char_width_ratios[str(codepoint)] = round(advance_width / units_per_em, 6)

    # Space width (codepoint 32)
    space_width = glyph_widths.get("32", units_per_em // 4)

    # Kerning pairs from kern table (if present)
    kern_pairs = {}
    kern_table = font.get("kern")
    if kern_table:
        for subtable in kern_table.kernTables:
            if hasattr(subtable, 'kernTable'):
                for (left_name, right_name), value in subtable.kernTable.items():
                    # Convert glyph names back to codepoints
                    left_cp = _glyph_to_codepoint(cmap, left_name)
                    right_cp = _glyph_to_codepoint(cmap, right_name)
                    if left_cp and right_cp:
                        key = f"{left_cp},{right_cp}"
                        kern_pairs[key] = value

    # Also try GPOS table for pair positioning (OpenType kerning)
    gpos = font.get("GPOS")
    if gpos and not kern_pairs:
        kern_pairs = _extract_gpos_kerning(gpos, cmap, font)

    font.close()

    return {
        "font_name": font_name,
        "font_style": font_style,
        "file": Path(font_path).name,
        "units_per_em": units_per_em,
        "hhea_ascender": hhea_ascender,
        "hhea_descender": hhea_descender,
        "hhea_line_gap": hhea_line_gap,
        "typo_ascender": typo_ascender,
        "typo_descender": typo_descender,
        "typo_line_gap": typo_line_gap,
        "win_ascent": win_ascent,
        "win_descent": win_descent,
        "use_typo_metrics": use_typo_metrics,
        "x_height": x_height,
        "cap_height": cap_height,
        "space_width": space_width,
        "chrome_line_height_units": chrome_line_height_units,
        "line_height_ratio": round(line_height_ratio, 6),
        "ascender_ratio": round(ascender_ratio, 6),
        "descender_ratio": round(descender_ratio, 6),
        "glyph_count": len(glyph_widths),
        "kern_pair_count": len(kern_pairs),
        "glyph_widths": glyph_widths,
        "char_width_ratios": char_width_ratios,
        "kern_pairs": kern_pairs,
    }


def _get_name(name_table, name_id: int) -> str:
    """Extract a name record from the font name table."""
    if not name_table:
        return None
    record = name_table.getName(name_id, 3, 1, 0x0409)  # Windows, Unicode BMP, English
    if record:
        return str(record)
    record = name_table.getName(name_id, 1, 0, 0)  # Mac, Roman, English
    if record:
        return str(record)
    return None


def _glyph_to_codepoint(cmap: dict, glyph_name: str) -> int:
    """Reverse lookup: glyph name → Unicode codepoint."""
    if not cmap:
        return None
    for cp, name in cmap.items():
        if name == glyph_name:
            return cp
    return None


def _extract_gpos_kerning(gpos, cmap, font) -> dict:
    """Extract kerning pairs from GPOS PairPos subtables."""
    kern_pairs = {}
    try:
        for lookup in gpos.table.LookupList.Lookup:
            if lookup.LookupType == 2:  # PairPos
                for subtable in lookup.SubTable:
                    if subtable.Format == 1:
                        # Format 1: individual pairs
                        for coverage_glyph, pair_set in zip(
                            subtable.Coverage.glyphs, subtable.PairSet
                        ):
                            for pvr in pair_set.PairValueRecord:
                                left_cp = _glyph_to_codepoint(cmap, coverage_glyph)
                                right_cp = _glyph_to_codepoint(cmap, pvr.SecondGlyph)
                                if left_cp and right_cp and pvr.Value1:
                                    x_advance = getattr(pvr.Value1, 'XAdvance', 0)
                                    if x_advance != 0:
                                        key = f"{left_cp},{right_cp}"
                                        kern_pairs[key] = x_advance
                    elif subtable.Format == 2:
                        # Format 2: class-based — skip for now, too complex
                        # (would need to expand class definitions into individual pairs)
                        pass
    except (AttributeError, TypeError):
        pass  # GPOS table structure doesn't match expected format
    return kern_pairs


def process_font(font_path: str, output_dir: Path) -> str:
    """Process a single font file and write JSON output."""
    print(f"  Extracting: {Path(font_path).name}")
    metrics = extract_metrics(font_path)

    # Output filename: FontName-Style.json (e.g., LiberationSerif-Regular.json)
    stem = Path(font_path).stem
    output_path = output_dir / f"{stem}.json"

    with open(output_path, "w") as f:
        json.dump(metrics, f, indent=2)

    print(f"    → {output_path.name}: {metrics['glyph_count']} glyphs, "
          f"{metrics['kern_pair_count']} kern pairs, "
          f"line-height ratio={metrics['line_height_ratio']:.4f}")
    return str(output_path)


def main():
    parser = argparse.ArgumentParser(
        description="Extract font metrics from TTF files for Redex layout engine"
    )
    parser.add_argument("--font", type=str, help="Path to a single TTF file to extract")
    parser.add_argument("--list", action="store_true", help="List available test fonts")
    parser.add_argument("--output", type=str, help="Override output directory")
    parser.add_argument("--compare", action="store_true",
                        help="Compare extracted metrics with hardcoded Racket values")
    args = parser.parse_args()

    output_dir = Path(args.output) if args.output else OUTPUT_DIR
    output_dir.mkdir(parents=True, exist_ok=True)

    if args.list:
        print(f"Available fonts in {FONT_DIR}:")
        for f in sorted(FONT_DIR.glob("*.ttf")):
            print(f"  {f.name}")
        return

    if args.font:
        # Single font extraction
        if not os.path.exists(args.font):
            print(f"ERROR: Font file not found: {args.font}", file=sys.stderr)
            sys.exit(1)
        process_font(args.font, output_dir)
        return

    # Extract all test fonts
    ttf_files = sorted(FONT_DIR.glob("*.ttf"))
    if not ttf_files:
        print(f"ERROR: No TTF files found in {FONT_DIR}", file=sys.stderr)
        sys.exit(1)

    print(f"Extracting metrics from {len(ttf_files)} fonts in {FONT_DIR}")
    print(f"Output directory: {output_dir}\n")

    for ttf_path in ttf_files:
        process_font(str(ttf_path), output_dir)

    print(f"\nDone. {len(ttf_files)} font metrics files written to {output_dir}")

    if args.compare:
        _compare_with_hardcoded(output_dir)


def _compare_with_hardcoded(output_dir: Path):
    """Compare extracted metrics with the hardcoded Racket values to show improvements."""
    print("\n" + "=" * 70)
    print("COMPARISON: Extracted metrics vs hardcoded Racket values")
    print("=" * 70)

    # Hardcoded Times ratios from reference-import.rkt (with -0.025 uppercase kerning hack)
    times_hardcoded = {
        'A': 0.722, 'B': 0.667, 'C': 0.667, 'D': 0.722, 'E': 0.611,
        'F': 0.556, 'G': 0.722, 'H': 0.722, 'I': 0.333, 'J': 0.389,
        'K': 0.722, 'L': 0.611, 'M': 0.889, 'N': 0.722, 'O': 0.722,
        'P': 0.556, 'Q': 0.722, 'R': 0.667, 'S': 0.556, 'T': 0.611,
        'U': 0.722, 'V': 0.722, 'W': 0.944, 'X': 0.722, 'Y': 0.722,
        'Z': 0.611, 'a': 0.444, 'b': 0.500, 'c': 0.444, 'd': 0.500,
        'e': 0.444, 'f': 0.333, 'g': 0.500, 'h': 0.500, 'i': 0.278,
        'j': 0.278, 'k': 0.500, 'l': 0.278, 'm': 0.778, 'n': 0.500,
        'o': 0.500, 'p': 0.500, 'q': 0.500, 'r': 0.333, 's': 0.389,
        't': 0.278, 'u': 0.500, 'v': 0.500, 'w': 0.722, 'x': 0.500,
        'y': 0.500, 'z': 0.444, ' ': 0.250,
    }
    times_lh_ratio_hardcoded = 1.107  # (891+216)/1000

    # Hardcoded Arial ratios
    arial_hardcoded = {
        'A': 0.667, 'B': 0.667, 'C': 0.722, 'D': 0.722, 'E': 0.667,
        'F': 0.611, 'G': 0.778, 'H': 0.722, 'I': 0.278, 'J': 0.500,
        'K': 0.667, 'L': 0.556, 'M': 0.833, 'N': 0.722, 'O': 0.778,
        'P': 0.667, 'Q': 0.778, 'R': 0.722, 'S': 0.667, 'T': 0.611,
        'U': 0.722, 'V': 0.667, 'W': 0.944, 'X': 0.667, 'Y': 0.667,
        'Z': 0.611, 'a': 0.556, 'b': 0.556, 'c': 0.500, 'd': 0.556,
        'e': 0.556, 'f': 0.278, 'g': 0.556, 'h': 0.556, 'i': 0.222,
        'j': 0.222, 'k': 0.500, 'l': 0.222, 'm': 0.833, 'n': 0.556,
        'o': 0.556, 'p': 0.556, 'q': 0.556, 'r': 0.333, 's': 0.500,
        't': 0.278, 'u': 0.556, 'v': 0.500, 'w': 0.722, 'x': 0.500,
        'y': 0.500, 'z': 0.500, ' ': 0.278,
    }
    arial_lh_ratio_hardcoded = 1.15  # (1854+434+67)/2048

    # Compare with LiberationSerif-Regular (Times equivalent)
    serif_path = output_dir / "LiberationSerif-Regular.json"
    if serif_path.exists():
        with open(serif_path) as f:
            serif = json.load(f)
        print(f"\n--- LiberationSerif-Regular (Times equivalent) ---")
        print(f"  Line-height ratio: hardcoded={times_lh_ratio_hardcoded:.4f}, "
              f"extracted={serif['line_height_ratio']:.4f}, "
              f"Δ={serif['line_height_ratio'] - times_lh_ratio_hardcoded:+.4f}")
        print(f"  Ascender ratio:    hardcoded=0.891, extracted={serif['ascender_ratio']:.4f}")
        print(f"  Descender ratio:   hardcoded=0.216, extracted={serif['descender_ratio']:.4f}")
        ratios = serif['char_width_ratios']
        diffs = []
        for ch, hardcoded_ratio in times_hardcoded.items():
            cp = str(ord(ch))
            if cp in ratios:
                extracted_ratio = ratios[cp]
                diff = extracted_ratio - hardcoded_ratio
                if abs(diff) > 0.001:
                    diffs.append((ch, hardcoded_ratio, extracted_ratio, diff))
        if diffs:
            print(f"  Width ratio differences (>{0.001}):")
            for ch, hc, ex, d in sorted(diffs, key=lambda x: -abs(x[3])):
                print(f"    '{ch}' (U+{ord(ch):04X}): hardcoded={hc:.3f}, extracted={ex:.6f}, Δ={d:+.4f}")
        else:
            print(f"  All character width ratios match within 0.001!")

    # Compare with LiberationSans-Regular (Arial equivalent)
    sans_path = output_dir / "LiberationSans-Regular.json"
    if sans_path.exists():
        with open(sans_path) as f:
            sans = json.load(f)
        print(f"\n--- LiberationSans-Regular (Arial equivalent) ---")
        print(f"  Line-height ratio: hardcoded={arial_lh_ratio_hardcoded:.4f}, "
              f"extracted={sans['line_height_ratio']:.4f}, "
              f"Δ={sans['line_height_ratio'] - arial_lh_ratio_hardcoded:+.4f}")
        print(f"  Ascender ratio:    hardcoded=0.905, extracted={sans['ascender_ratio']:.4f}")
        print(f"  Descender ratio:   hardcoded=0.212, extracted={sans['descender_ratio']:.4f}")
        ratios = sans['char_width_ratios']
        diffs = []
        for ch, hardcoded_ratio in arial_hardcoded.items():
            cp = str(ord(ch))
            if cp in ratios:
                extracted_ratio = ratios[cp]
                diff = extracted_ratio - hardcoded_ratio
                if abs(diff) > 0.001:
                    diffs.append((ch, hardcoded_ratio, extracted_ratio, diff))
        if diffs:
            print(f"  Width ratio differences (>{0.001}):")
            for ch, hc, ex, d in sorted(diffs, key=lambda x: -abs(x[3])):
                print(f"    '{ch}' (U+{ord(ch):04X}): hardcoded={hc:.3f}, extracted={ex:.6f}, Δ={d:+.4f}")
        else:
            print(f"  All character width ratios match within 0.001!")


if __name__ == "__main__":
    main()
