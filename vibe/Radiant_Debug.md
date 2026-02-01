# Radiant Layout Debugging Suggestions

Practical improvements for debugging CSS layout issues in the Radiant engine.

## 1. Visual Diff Tool

Create a side-by-side HTML comparison view:

```bash
# Add a command like:
./lambda.exe layout-diff test.html -o diff.html
```

This would generate an HTML file showing:
- Browser reference (screenshot or overlay)
- Your layout (rendered)
- Highlighted differences with measurements
- Color-coded elements: green=match, red=mismatch, yellow=close

## 2. Focused Element Tracing

Add a CSS selector-based debug mode:

```bash
./lambda.exe layout test.html --trace="td:nth-child(2)"
./lambda.exe layout test.html --trace="#myElement"
./lambda.exe layout test.html --trace=".cell.highlight"
```

This would print detailed step-by-step calculations only for that element:
- When its dimensions are set (and by which code path)
- Each vertical-align/horizontal-align adjustment
- Parent/child relationships and inheritance
- Box model values (margin, border, padding, content)

Example output:
```
[TRACE] #myElement
  ├─ Initial: x=0, y=0, w=auto, h=auto
  ├─ resolve_css_style: given_width=200, given_height=auto
  ├─ layout_block: content_width=180 (200 - padding 20)
  ├─ layout_children: total_child_height=50
  ├─ Final: x=10, y=30, w=200, h=70
  └─ Expected (reference): x=10, y=30, w=200, h=70 ✓
```

## 3. Structured View Tree Output

The current `view_tree.txt` output is verbose. Add output format options:

```bash
./lambda.exe layout test.html --format=json      # Machine-readable
./lambda.exe layout test.html --format=compact   # One line per element
./lambda.exe layout test.html --format=table     # Tabular with columns
./lambda.exe layout test.html --filter=table     # Only table-related views
./lambda.exe layout test.html --filter=flex      # Only flex containers/items
./lambda.exe layout test.html --depth=3          # Limit depth
```

Compact format example:
```
html          (0,0)     1200×800
  body        (8,16)    1184×768
    table     (0,34)    400×300   [border-collapse]
      tbody   (2,2)     396×296
        tr    (0,0)     396×50
          td  (0,0)     100×50    [valign:middle]
          td  (100,0)   100×50    [valign:bottom]
```

## 4. Quick Assertions in Test Files

Add inline assertions in test HTML files that can be validated without browser references:

```html
<!DOCTYPE html>
<html>
<head>
  <style>
    .box { width: 100px; height: 50px; }
  </style>
  <!-- LAYOUT-EXPECT: .box { width: 100, height: 50 } -->
  <!-- LAYOUT-EXPECT: #cell1 { y: 20, height: 50 } -->
  <!-- LAYOUT-EXPECT: table { width: 400 } -->
</head>
<body>
  <div class="box" id="cell1">Content</div>
</body>
</html>
```

The test runner validates these assertions directly:
```bash
./lambda.exe layout test.html --validate-assertions
# Output:
# ✓ .box { width: 100, height: 50 }
# ✗ #cell1 { y: 20, height: 50 } - actual y=25
# ✓ table { width: 400 }
```

## 5. Log Categories

The log system supports levels, but categories would improve filtering:

```cpp
// In code:
log_table("Row %d height: %d", row_idx, height);
log_flex("Flex item %d: grow=%f", i, grow);
log_grid("Grid track %d: size=%f", track, size);
log_inline("Line box %d: baseline=%f", line, baseline);
log_cascade("Property %s: value=%s (from %s)", prop, val, source);
```

Run with specific categories:
```bash
./lambda.exe layout test.html --log-category=table
./lambda.exe layout test.html --log-category=table,flex
./lambda.exe layout test.html --log-category=all
```

Implementation in `lib/log.h`:
```c
#define LOG_CAT_TABLE  (1 << 0)
#define LOG_CAT_FLEX   (1 << 1)
#define LOG_CAT_GRID   (1 << 2)
#define LOG_CAT_INLINE (1 << 3)

#define log_table(...) log_category(LOG_CAT_TABLE, __VA_ARGS__)
```

## 6. Checkpoint Snapshots

Save intermediate layout states for debugging multi-pass algorithms:

```cpp
// In code:
LAYOUT_CHECKPOINT("after_column_width_calculation");
LAYOUT_CHECKPOINT("after_row_height_calculation");
LAYOUT_CHECKPOINT("after_vertical_alignment");
```

Compare checkpoints:
```bash
./lambda.exe layout-checkpoints test.html
# Output:
# Checkpoint: after_column_width_calculation
#   table: 400×0
#   td[0]: 100×0, td[1]: 150×0, td[2]: 150×0
#
# Checkpoint: after_row_height_calculation  
#   table: 400×120
#   tr[0]: 400×60, tr[1]: 400×60
#
# Checkpoint: after_vertical_alignment
#   td[0,0]: content y=5→20 (middle align, +15)
```

## 7. Regression Test Generator

Automatically generate reference data from browser:

```bash
./lambda.exe generate-reference test.html
# Opens browser, captures layout, saves to test/layout/reference/test.json
```

Or batch mode:
```bash
./lambda.exe generate-reference test/layout/data/table/*.html --browser=chrome
```

## 8. Interactive Debug Mode

Launch a debug session with step-through capability:

```bash
./lambda.exe layout test.html --interactive
```

Commands:
```
> step          # Process next element
> continue      # Run to completion
> break #id     # Break when element #id is processed
> print #id     # Show current state of element
> watch #id.y   # Break when #id.y changes
> diff          # Show current vs reference
> quit
```

## Priority Implementation Order

1. **Log Categories** - Low effort, high value for targeted debugging
2. **Compact View Tree** - Makes manual inspection much faster
3. **Element Tracing** - Essential for understanding specific element issues
4. **Inline Assertions** - Enables quick test creation without browser
5. **Visual Diff** - High effort but very valuable for complex layouts
6. **Checkpoints** - Useful for multi-pass algorithm debugging
