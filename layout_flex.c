#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

// enums (unchanged)
typedef enum { DIR_ROW, DIR_ROW_REVERSE, DIR_COLUMN, DIR_COLUMN_REVERSE } FlexDirection;
typedef enum { WRAP_NOWRAP, WRAP_WRAP, WRAP_WRAP_REVERSE } FlexWrap;
typedef enum { JUSTIFY_START, JUSTIFY_END, JUSTIFY_CENTER, JUSTIFY_SPACE_BETWEEN, JUSTIFY_SPACE_AROUND, JUSTIFY_SPACE_EVENLY } JustifyContent;
typedef enum { 
    ALIGN_START, ALIGN_END, ALIGN_CENTER, ALIGN_BASELINE, ALIGN_STRETCH,
    ALIGN_SPACE_BETWEEN, ALIGN_SPACE_AROUND, ALIGN_SPACE_EVENLY
} AlignType;
typedef enum { VIS_VISIBLE, VIS_HIDDEN, VIS_COLLAPSE } Visibility;
typedef enum { POS_STATIC, POS_ABSOLUTE } PositionType;
typedef enum { WM_HORIZONTAL_TB, WM_VERTICAL_RL, WM_VERTICAL_LR } WritingMode;
typedef enum { TD_LTR, TD_RTL } TextDirection;

// structs (unchanged)
typedef struct { int x, y; } Point;

typedef struct {
    Point pos;
    int width, height;
    int minWidth, maxWidth;
    int minHeight, maxHeight;
    int flexBasis;  // -1 for auto
    float flexGrow, flexShrink;
    int margin[4];
    AlignType alignSelf;
    int order;
    Visibility visibility;
    PositionType position;
    float aspectRatio;
} FlexItem;

typedef struct {
    FlexItem** items;
    int itemCount;
    int totalBaseSize;
    int height;
} FlexLine;

typedef struct {
    int width, height;
    FlexDirection direction;
    FlexWrap wrap;
    JustifyContent justify;
    AlignType alignItems;
    AlignType alignContent;
    int gap;
    FlexItem* items;
    int itemCount;
    WritingMode writingMode;
    TextDirection textDirection;
} FlexContainer;

static void initialize_items(FlexContainer* container, FlexItem* layoutItems, int* layoutCount);
static void create_flex_lines(FlexContainer* container, FlexItem* layoutItems, int layoutCount, FlexLine** lines, int* lineCount);
static void process_flex_line(FlexContainer* container, FlexLine* line, FlexLine* lines, int lineCount, float mainSize, float crossSize, float* crossPos, int isRow, int isReverse);
static void apply_flex_adjustments(FlexLine* line, FlexLine* lines, float freeSpace);
static void position_items_main_axis(FlexContainer* container, FlexLine* line, float mainSize, int isRow, int isReverse);
static void position_items_cross_axis(FlexContainer* container, FlexLine* line, float crossSize, float crossPos, int isRow);
static void update_original_items(FlexContainer* container, FlexItem* layoutItems, int layoutCount);

// Helper functions
float clamp(float value, float min, float max) {
    float result = (max != 0) ? fmin(fmax(value, min), max) : fmax(value, min);
    printf("clamp(%.1f, %.1f, %.1f) = %.1f\n", value, min, max, result);
    return result;
}

int resolve_flex_basis(FlexItem* item) {
    int basis;
    if (item->flexBasis == -1) {
        basis = item->width > 0 ? item->width : 0;
    } else {
        basis = (item->flexBasis > 0) ? item->flexBasis : item->width;
    }
    printf("resolve_flex_basis: width=%d, flexBasis=%d -> %d\n", item->width, item->flexBasis, basis);
    assert(basis >= 0 && "Flex basis must be non-negative");
    return basis;
}

void apply_constraints(FlexItem* item) {
    int oldWidth = item->width, oldHeight = item->height;

    // Adjust dimensions based on aspect ratio
    if (item->aspectRatio > 0) {
        if (item->width > 0 && item->height == 0) {
            item->height = (int)(item->width / item->aspectRatio);
        } else if (item->height > 0 && item->width == 0) {
            item->width = (int)(item->height * item->aspectRatio);
        }
    }

    item->width = clamp(item->width, item->minWidth, item->maxWidth);
    item->height = clamp(item->height, item->minHeight, item->maxHeight);

    // Reapply aspect ratio constraints after clamping
    if (item->aspectRatio > 0) {
        if (item->width > 0 && item->height == 0) {
            item->height = (int)(item->width / item->aspectRatio);
        } else if (item->height > 0 && item->width == 0) {
            item->width = (int)(item->height * item->aspectRatio);
        }
    }

    printf("apply_constraints: width %d -> %d, height %d -> %d\n", oldWidth, item->width, oldHeight, item->height);
}

// Flex layout sub-functions
static void initialize_items(FlexContainer* container, FlexItem* layoutItems, int* layoutCount) {
    for (int i = 0; i < container->itemCount; i++) {
        if (container->items[i].position != POS_ABSOLUTE && container->items[i].visibility != VIS_HIDDEN) {
            layoutItems[*layoutCount] = container->items[i];
            layoutItems[*layoutCount].pos = (Point){0, 0};
            if (layoutItems[*layoutCount].alignSelf == ALIGN_START) {
                layoutItems[*layoutCount].alignSelf = container->alignItems;
            }
            layoutItems[*layoutCount].width = resolve_flex_basis(&layoutItems[*layoutCount]);
            apply_constraints(&layoutItems[*layoutCount]);
            (*layoutCount)++;
        }
    }
    printf("Filtered %d items for layout\n", *layoutCount);
}

static void create_flex_lines(FlexContainer* container, FlexItem* layoutItems, int layoutCount, 
                            FlexLine** lines, int* lineCount) {
    float remainingSpace = (container->direction == DIR_ROW || container->direction == DIR_ROW_REVERSE) 
                         ? container->width : container->height;
    FlexLine currentLine = { .items = NULL, .itemCount = 0, .totalBaseSize = 0, .height = 0 };
    
    *lineCount = 0;
    *lines = NULL;

    for (int i = 0; i < layoutCount; i++) {
        float itemSize = layoutItems[i].width;
        float spaceNeeded = itemSize + (currentLine.itemCount > 0 ? container->gap : 0);
        
        if (container->wrap == WRAP_NOWRAP || remainingSpace >= spaceNeeded) {
            currentLine.items = realloc(currentLine.items, (currentLine.itemCount + 1) * sizeof(FlexItem*));
            currentLine.items[currentLine.itemCount++] = &layoutItems[i];
            currentLine.totalBaseSize += spaceNeeded;
            currentLine.height = fmax(currentLine.height, layoutItems[i].height);
            remainingSpace -= spaceNeeded;
        }
        else {
            if (currentLine.itemCount > 0) {
                *lines = realloc(*lines, (*lineCount + 1) * sizeof(FlexLine));
                (*lines)[(*lineCount)++] = currentLine;
                currentLine = (FlexLine){ .items = NULL, .itemCount = 0, .totalBaseSize = 0, .height = 0 };
            }
            currentLine.items = realloc(currentLine.items, sizeof(FlexItem*));
            currentLine.items[0] = &layoutItems[i];
            currentLine.itemCount = 1;
            currentLine.totalBaseSize = itemSize;
            currentLine.height = layoutItems[i].height;
            remainingSpace = (container->direction == DIR_ROW || container->direction == DIR_ROW_REVERSE) 
                           ? container->width - itemSize : container->height - itemSize;
        }
    }
    if (currentLine.itemCount > 0) {
        *lines = realloc(*lines, (*lineCount + 1) * sizeof(FlexLine));
        (*lines)[(*lineCount)++] = currentLine;
    } else {
        free(currentLine.items);
    }
    printf("Created %d lines\n", *lineCount);
}

static void process_flex_line(FlexContainer* container, FlexLine* line, FlexLine* lines, int lineCount,
                            float mainSize, float containerCrossSize, float* crossPos, int isRow, int isReverse) {
    float freeSpace = mainSize - line->totalBaseSize;
    if (mainSize <= 0) {
        freeSpace = 0;
        line->totalBaseSize = 0;
        for (int i = 0; i < line->itemCount; i++) {
            line->totalBaseSize = 0;
        }
    } else {
        apply_flex_adjustments(line, lines, freeSpace);
        
        line->totalBaseSize = 0;
        for (int i = 0; i < line->itemCount; i++) {
            line->totalBaseSize += (isRow ? line->items[i]->width : line->items[i]->height) + 
                                 (i > 0 ? container->gap : 0);
        }
        freeSpace = mainSize - line->totalBaseSize;
    }
    
    position_items_main_axis(container, line, mainSize, isRow, isReverse);
    position_items_cross_axis(container, line, containerCrossSize, *crossPos, isRow);  // Use containerCrossSize
    
    printf("Processed line %ld: crossPos=%.1f, height=%d\n", line - lines, *crossPos, line->height);
}

static void apply_flex_adjustments(FlexLine* line, FlexLine* lines, float freeSpace) {
    float totalGrow = 0, totalShrink = 0;
    for (int i = 0; i < line->itemCount; i++) {
        totalGrow += line->items[i]->flexGrow;
        totalShrink += line->items[i]->flexShrink;
    }
    printf("Line %ld: freeSpace=%.1f, totalGrow=%.1f, totalShrink=%.1f\n", 
           line - lines, freeSpace, totalGrow, totalShrink);

    if (freeSpace > 0 && totalGrow > 0) {
        float remainingSpace = freeSpace;
        for (int i = 0; i < line->itemCount; i++) {
            if (line->items[i]->flexGrow > 0) {
                float growAmount = (remainingSpace * line->items[i]->flexGrow) / totalGrow;
                line->items[i]->width += (int)roundf(growAmount);

                // Adjust height based on aspect ratio
                if (line->items[i]->aspectRatio > 0) {
                    line->items[i]->height = (int)(line->items[i]->width / line->items[i]->aspectRatio);
                }

                apply_constraints(line->items[i]);
                printf("Grow item %d: width=%d, height=%d\n", i, line->items[i]->width, line->items[i]->height);
            }
        }
    } else if (freeSpace < 0 && totalShrink > 0) {
        float remainingSpace = fabs(freeSpace);
        for (int i = 0; i < line->itemCount; i++) {
            if (line->items[i]->flexShrink > 0) {
                float shrinkAmount = (remainingSpace * line->items[i]->flexShrink) / totalShrink;
                line->items[i]->width -= (int)roundf(shrinkAmount);

                // Adjust height based on aspect ratio
                if (line->items[i]->aspectRatio > 0) {
                    line->items[i]->height = (int)(line->items[i]->width / line->items[i]->aspectRatio);
                }

                apply_constraints(line->items[i]);
                printf("Shrink item %d: width=%d, height=%d\n", i, line->items[i]->width, line->items[i]->height);
            }
        }
    }
}

static void position_items_main_axis(FlexContainer* container, FlexLine* line, float mainSize, 
                                   int isRow, int isReverse) {
    float freeSpace = mainSize - line->totalBaseSize;
    float mainPos = 0, spacing = 0;
    if (mainSize > 0) {
        switch (container->justify) {
            case JUSTIFY_END: mainPos = freeSpace; break;
            case JUSTIFY_CENTER: mainPos = freeSpace / 2; break;
            case JUSTIFY_SPACE_BETWEEN: 
                spacing = (line->itemCount > 1) ? freeSpace / (line->itemCount - 1) : 0; break;
            case JUSTIFY_SPACE_AROUND: 
                spacing = freeSpace / line->itemCount; mainPos = spacing / 2; break;
            case JUSTIFY_SPACE_EVENLY: 
                spacing = freeSpace / (line->itemCount + 1); mainPos = spacing; break;
            default: break;
        }
    }
    printf("Main axis: mainPos=%.1f, spacing=%.1f\n", mainPos, spacing);

    if (isReverse) {
        float currentPos = mainSize - mainPos;
        for (int i = 0; i < line->itemCount; i++) {
            int idx = line->itemCount - 1 - i;
            float itemSize = isRow ? line->items[idx]->width : line->items[idx]->height;
            currentPos -= itemSize;
            if (isRow) {
                line->items[idx]->pos.x = mainSize <= 0 ? 0 : (int)currentPos;
            } else {
                line->items[idx]->pos.y = mainSize <= 0 ? 0 : (int)currentPos;
            }
            if (i < line->itemCount - 1 && mainSize > 0) {
                currentPos -= container->gap + (container->justify >= JUSTIFY_SPACE_BETWEEN ? spacing : 0);
            }
            printf("Item %d: pos=%d\n", idx, isRow ? line->items[idx]->pos.x : line->items[idx]->pos.y);
        }
    } else {
        float currentPos = mainPos;
        for (int i = 0; i < line->itemCount; i++) {
            int idx = i;
            if (isRow) {
                line->items[idx]->pos.x = mainSize <= 0 ? 0 : (int)currentPos;
            } else {
                line->items[idx]->pos.y = mainSize <= 0 ? 0 : (int)currentPos;
            }
            if (mainSize > 0) {
                currentPos += (isRow ? line->items[idx]->width : line->items[idx]->height);
                if (i < line->itemCount - 1) {
                    currentPos += container->gap + (container->justify >= JUSTIFY_SPACE_BETWEEN ? spacing : 0);
                }
            }
            printf("Item %d: pos=%d\n", idx, isRow ? line->items[idx]->pos.x : line->items[idx]->pos.y);
        }
    }
}

static void position_items_cross_axis(FlexContainer* container, FlexLine* line, float crossSize, 
                                    float crossPos, int isRow) {
    for (int i = 0; i < line->itemCount; i++) {
        float itemCrossSize = isRow ? line->items[i]->height : line->items[i]->width;
        float itemCrossPos = crossPos;
        
        switch (line->items[i]->alignSelf) {
            case ALIGN_END: itemCrossPos = crossPos + (crossSize - itemCrossSize); break;
            case ALIGN_CENTER: itemCrossPos = crossPos + (crossSize - itemCrossSize) / 2; break;
            case ALIGN_STRETCH: 
                if (isRow) line->items[i]->height = crossSize;
                else line->items[i]->width = crossSize;
                itemCrossPos = crossPos;
                break;
            default: itemCrossPos = crossPos; break;
        }
        
        if (isRow) {
            line->items[i]->pos.y = itemCrossPos;
        } else {
            line->items[i]->pos.x = itemCrossPos;
        }
        printf("Item %d: crossPos=%.1f\n", i, itemCrossPos);
    }
}

static void update_original_items(FlexContainer* container, FlexItem* layoutItems, int layoutCount) {
    int k = 0;
    for (int i = 0; i < container->itemCount; i++) {
        if (container->items[i].position != POS_ABSOLUTE && container->items[i].visibility != VIS_HIDDEN) {
            container->items[i] = layoutItems[k];
            printf("Final item %d: x=%d, y=%d, w=%d, h=%d\n",
                   i, container->items[i].pos.x, container->items[i].pos.y,
                   container->items[i].width, container->items[i].height);
            k++;
        }
    }
}

// flex layout main function
void layout_flex_container(FlexContainer* container) {
    printf("\n=== Starting layout_flex_container ===\n");
    printf("Container: width=%d, height=%d, gap=%d, items=%d, justify=%d, alignItems=%d, alignContent=%d\n", 
           container->width, container->height, container->gap, container->itemCount, container->justify, 
           container->alignItems, container->alignContent);

    int isRow = (container->direction == DIR_ROW || container->direction == DIR_ROW_REVERSE);
    int isReverse = (container->direction == DIR_ROW_REVERSE || container->direction == DIR_COLUMN_REVERSE);
    float mainSize = isRow ? container->width : container->height;
    float crossSize = isRow ? container->height : container->width;
    if (mainSize <= 0) mainSize = 0;
    if (crossSize <= 0) crossSize = 0;

    FlexItem* layoutItems = malloc(container->itemCount * sizeof(FlexItem));
    int layoutCount = 0;
    initialize_items(container, layoutItems, &layoutCount);

    FlexLine* lines = NULL;
    int lineCount = 0;
    create_flex_lines(container, layoutItems, layoutCount, &lines, &lineCount);

    float totalCrossSize = 0;
    for (int l = 0; l < lineCount; l++) {
        totalCrossSize += lines[l].height;
        if (l < lineCount - 1) totalCrossSize += container->gap;
    }
    float freeCrossSpace = crossSize - totalCrossSize;
    float crossPos = 0, crossSpacing = 0;

    if (container->wrap != WRAP_NOWRAP && lineCount > 1 && crossSize > 0) {
        switch (container->alignContent) {
            case ALIGN_END: crossPos = freeCrossSpace; break;
            case ALIGN_CENTER: crossPos = freeCrossSpace / 2; break;
            case ALIGN_SPACE_BETWEEN: crossSpacing = freeCrossSpace / (lineCount - 1); break;
            case ALIGN_SPACE_AROUND: 
                crossSpacing = freeCrossSpace / lineCount; 
                crossPos = crossSpacing / 2; 
                break;
            case ALIGN_SPACE_EVENLY: 
                crossSpacing = freeCrossSpace / (lineCount + 1); 
                crossPos = crossSpacing; 
                break;
            case ALIGN_STRETCH: 
                float stretchFactor = crossSize / totalCrossSize;
                for (int l = 0; l < lineCount; l++) {
                    lines[l].height = (int)(lines[l].height * stretchFactor);
                    // Apply stretched height to each item in the line
                    for (int i = 0; i < lines[l].itemCount; i++) {
                        if (isRow) {
                            lines[l].items[i]->height = lines[l].height;
                        } else {
                            lines[l].items[i]->width = lines[l].height;
                        }
                    }
                }
                totalCrossSize = crossSize;
                freeCrossSpace = 0;
                break;
            default: break;
        }
    }
    printf("Align-content: freeCrossSpace=%.1f, crossPos=%.1f, crossSpacing=%.1f\n", 
           freeCrossSpace, crossPos, crossSpacing);

    if (container->wrap == WRAP_WRAP_REVERSE && isRow) {
        float currentCrossPos = crossSize - crossPos;
        for (int l = 0; l < lineCount; l++) {
            currentCrossPos -= lines[l].height;
            process_flex_line(container, &lines[l], lines, lineCount, mainSize, crossSize, 
                            &currentCrossPos, isRow, isReverse);
            if (l < lineCount - 1) {
                currentCrossPos -= container->gap + (container->alignContent >= ALIGN_SPACE_BETWEEN ? crossSpacing : 0);
            }
        }
    } else {
        float currentCrossPos = crossPos;
        for (int l = 0; l < lineCount; l++) {
            process_flex_line(container, &lines[l], lines, lineCount, mainSize, crossSize, 
                            &currentCrossPos, isRow, isReverse);
            if (l < lineCount - 1) {
                currentCrossPos += lines[l].height + container->gap + 
                                 (container->alignContent >= ALIGN_SPACE_BETWEEN ? crossSpacing : 0);
            }
        }
    }

    update_original_items(container, layoutItems, layoutCount);

    free(layoutItems);
    for (int i = 0; i < lineCount; i++) free(lines[i].items);
    free(lines);
    printf("=== Layout completed ===\n");
}

void free_flex_container(FlexContainer* container) {
    if (container->items) free(container->items);
}