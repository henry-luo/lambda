#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

// structs
typedef struct { int x, y; } Point;

typedef struct {
    Point pos;
    int width, height;
    int minWidth, maxWidth;
    int minHeight, maxHeight;
    int flexBasis;  // -1 represents 'auto'
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
    int rowGap, columnGap;
    FlexItem* items;
    int itemCount;
    WritingMode writingMode;
    TextDirection textDirection;
} FlexContainer;

// Function declarations
static void initialize_items(FlexContainer* container, FlexItem* layoutItems, int* layoutCount);
static void create_flex_lines(FlexContainer* container, FlexItem* layoutItems, int layoutCount, FlexLine** lines, int* lineCount);
static void process_flex_line(FlexContainer* container, FlexLine* line, FlexLine* lines, float mainSize, float crossPos, int lineCount, int isRow, int isReverse);
static void apply_flex_adjustments(FlexLine* line, FlexLine* lines, float freeSpace);
static void position_items_main_axis(FlexContainer* container, FlexLine* line, float mainSize, int isRow, int isReverse);
static void position_items_cross_axis(FlexContainer* container, FlexLine* line, float crossSize, float crossPos, int isRow);
static void update_original_items(FlexContainer* container, FlexItem* layoutItems, int layoutCount);

// Helper functions
float clamp(float value, float min, float max) {
    float result = (max != 0) ? fminf(fmaxf(value, min), max) : fmaxf(value, min);
    printf("clamp(%.1f, %.1f, %.1f) = %.1f\n", value, min, max, result);
    return result;
}

float resolve_flex_basis(FlexItem* item) {
    float basis = item->flexBasis;
    if (basis == -1) {  // 'auto' case
        basis = item->width > 0 ? item->width : 0;
        printf("resolve_flex_basis: width=%.1f, flexBasis=%.1f -> %.1f\n", item->width, item->flexBasis, basis);
    } else if (basis < 0) {
        basis = item->width;  // Default instead of assert
        printf("resolve_flex_basis: width=%.1f, flexBasis=%.1f -> %.1f\n", item->width, item->flexBasis, basis);
    } else {
        printf("resolve_flex_basis: width=%.1f, flexBasis=%.1f -> %.1f\n", item->width, item->flexBasis, basis);
    }
    return basis;
}

void apply_constraints(FlexItem* item) {
    float oldWidth = item->width, oldHeight = item->height;
    if (item->aspectRatio > 0) {
        if (item->width <= 0 && item->height > 0) {
            item->width = item->height * item->aspectRatio;
        } else if (item->height <= 0 && item->width > 0) {
            item->height = item->width / item->aspectRatio;
        }
    }
    item->width = clamp(item->width, item->minWidth, item->maxWidth);
    item->height = clamp(item->height, item->minHeight, item->maxHeight);
    printf("apply_constraints: width %.1f -> %.1f, height %.1f -> %.1f\n", oldWidth, item->width, oldHeight, item->height);
}

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
    float gap = (container->direction == DIR_ROW || container->direction == DIR_ROW_REVERSE) 
                ? container->columnGap : container->rowGap;
    FlexLine currentLine = { .items = NULL, .itemCount = 0, .totalBaseSize = 0, .height = 0 };
    
    *lineCount = 0;
    *lines = NULL;

    for (int i = 0; i < layoutCount; i++) {
        float itemSize = layoutItems[i].width;
        float spaceNeeded = itemSize + (currentLine.itemCount > 0 ? gap : 0);
        
        if (container->wrap == WRAP_NOWRAP || remainingSpace >= spaceNeeded) {
            currentLine.items = realloc(currentLine.items, (currentLine.itemCount + 1) * sizeof(FlexItem*));
            currentLine.items[currentLine.itemCount++] = &layoutItems[i];
            currentLine.totalBaseSize += spaceNeeded;
            currentLine.height = fmaxf(currentLine.height, layoutItems[i].height);
            remainingSpace -= spaceNeeded;
        } else {
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

static void apply_flex_adjustments(FlexLine* line, FlexLine* lines, float freeSpace) {
    float totalGrow = 0, totalShrink = 0;
    for (int i = 0; i < line->itemCount; i++) {
        totalGrow += line->items[i]->flexGrow;
        totalShrink += line->items[i]->flexShrink;
    }
    printf("Line %ld: freeSpace=%.1f, totalGrow=%.1f, totalShrink=%.1f\n", 
           line - lines, freeSpace, totalGrow, totalShrink);

    if (freeSpace > 0 && totalGrow > 0) {
        float growFactor = freeSpace / totalGrow;
        for (int i = 0; i < line->itemCount; i++) {
            if (line->items[i]->flexGrow > 0) {
                float growAmount = growFactor * line->items[i]->flexGrow;
                line->items[i]->width += growAmount;
                apply_constraints(line->items[i]);
                printf("Grow item %d: width=%.1f\n", i, line->items[i]->width);
            }
        }
    } else if (freeSpace < 0 && totalShrink > 0) {
        float shrinkFactor = -freeSpace / totalShrink;
        for (int i = 0; i < line->itemCount; i++) {
            if (line->items[i]->flexShrink > 0) {
                float shrinkAmount = shrinkFactor * line->items[i]->flexShrink;
                line->items[i]->width = fmaxf(0, line->items[i]->width - shrinkAmount);
                apply_constraints(line->items[i]);
                printf("Shrink item %d: width=%.1f\n", i, line->items[i]->width);
            }
        }
    }
}

static void position_items_main_axis(FlexContainer* container, FlexLine* line, float mainSize, 
                                   int isRow, int isReverse) {
    float gap = isRow ? container->columnGap : container->rowGap;
    float totalItemSize = 0;
    for (int i = 0; i < line->itemCount; i++) {
        totalItemSize += (isRow ? line->items[i]->width : line->items[i]->height);
        if (i > 0) totalItemSize += gap;
    }
    
    float freeSpace = mainSize - totalItemSize;
    float mainPos = 0, spacing = 0;
    
    if (freeSpace > 0) {
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
        for (int i = line->itemCount - 1; i >= 0; i--) {
            float itemSize = isRow ? line->items[i]->width : line->items[i]->height;
            currentPos -= itemSize;
            if (isRow) line->items[i]->pos.x = currentPos;
            else line->items[i]->pos.y = currentPos;
            currentPos -= (i > 0) ? gap + spacing : 0;
            printf("Item %d: pos=%.1f\n", i, isRow ? line->items[i]->pos.x : line->items[i]->pos.y);
        }
    } else {
        float currentPos = mainPos;
        for (int i = 0; i < line->itemCount; i++) {
            if (isRow) line->items[i]->pos.x = currentPos;
            else line->items[i]->pos.y = currentPos;
            currentPos += (isRow ? line->items[i]->width : line->items[i]->height);
            if (i < line->itemCount - 1) currentPos += gap + spacing;
            printf("Item %d: pos=%.1f\n", i, isRow ? line->items[i]->pos.x : line->items[i]->pos.y);
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

static void process_flex_line(FlexContainer* container, FlexLine* line, FlexLine* lines, float mainSize, float crossPos, 
                            int lineCount, int isRow, int isReverse) {
    float gap = isRow ? container->columnGap : container->rowGap;
    line->totalBaseSize = 0;
    for (int i = 0; i < line->itemCount; i++) {
        line->totalBaseSize += (isRow ? line->items[i]->width : line->items[i]->height);
        if (i > 0) line->totalBaseSize += gap;
    }
    
    float freeSpace = mainSize - line->totalBaseSize;
    apply_flex_adjustments(line, lines, freeSpace);
    
    // Recalculate total size after adjustments
    line->totalBaseSize = 0;
    for (int i = 0; i < line->itemCount; i++) {
        line->totalBaseSize += (isRow ? line->items[i]->width : line->items[i]->height);
        if (i > 0) line->totalBaseSize += gap;
    }
    
    position_items_main_axis(container, line, mainSize, isRow, isReverse);
    position_items_cross_axis(container, line, isRow ? container->height : container->width, crossPos, isRow);
}

static void update_original_items(FlexContainer* container, FlexItem* layoutItems, int layoutCount) {
    int k = 0;
    for (int i = 0; i < container->itemCount; i++) {
        if (container->items[i].position != POS_ABSOLUTE && container->items[i].visibility != VIS_HIDDEN) {
            container->items[i] = layoutItems[k];
            container->items[i].pos.x = roundf(container->items[i].pos.x);
            container->items[i].pos.y = roundf(container->items[i].pos.y);
            container->items[i].width = roundf(container->items[i].width);
            container->items[i].height = roundf(container->items[i].height);
            printf("Final item %d: x=%.1f, y=%.1f, w=%.1f, h=%.1f\n",
                   i, container->items[i].pos.x, container->items[i].pos.y,
                   container->items[i].width, container->items[i].height);
            k++;
        }
    }
}

void layout_flex_container(FlexContainer* container) {
    printf("\n=== Starting layout_flex_container ===\n");
    printf("Container: width=%.1f, height=%.1f, gap=%.1f, items=%d, justify=%d, alignItems=%d\n", 
           container->width, container->height, container->columnGap, container->itemCount, container->justify, container->alignItems);

    int isRow = (container->direction == DIR_ROW || container->direction == DIR_ROW_REVERSE);
    int isReverse = (container->direction == DIR_ROW_REVERSE || container->direction == DIR_COLUMN_REVERSE);
    float mainSize = isRow ? container->width : container->height;
    float crossSize = isRow ? container->height : container->width;
    float crossGap = isRow ? container->rowGap : container->columnGap;

    FlexItem* layoutItems = malloc(container->itemCount * sizeof(FlexItem));
    int layoutCount = 0;
    initialize_items(container, layoutItems, &layoutCount);

    FlexLine* lines = NULL;
    int lineCount = 0;
    create_flex_lines(container, layoutItems, layoutCount, &lines, &lineCount);

    float totalCrossSize = 0;
    for (int l = 0; l < lineCount; l++) {
        totalCrossSize += lines[l].height;
        if (l < lineCount - 1) totalCrossSize += crossGap;
    }

    float crossPos = 0;
    float freeCrossSpace = crossSize - totalCrossSize;
    float lineSpacing = 0;

    if (lineCount > 1 && freeCrossSpace > 0) {
        switch (container->alignContent) {
            case ALIGN_END: crossPos = freeCrossSpace; break;
            case ALIGN_CENTER: crossPos = freeCrossSpace / 2; break;
            case ALIGN_SPACE_BETWEEN: 
                lineSpacing = freeCrossSpace / (lineCount - 1); break;
            case ALIGN_SPACE_AROUND: 
                lineSpacing = freeCrossSpace / lineCount; crossPos = lineSpacing / 2; break;
            case ALIGN_SPACE_EVENLY: 
                lineSpacing = freeCrossSpace / (lineCount + 1); crossPos = lineSpacing; break;
            default: break;
        }
    }

    if (container->wrap == WRAP_WRAP_REVERSE && isRow) {
        crossPos = crossSize - totalCrossSize - (lineCount - 1) * crossGap;
        for (int l = lineCount - 1; l >= 0; l--) {
            process_flex_line(container, &lines[l], lines, mainSize, crossPos, lineCount, isRow, isReverse);
            crossPos += lines[l].height + crossGap + lineSpacing;
        }
    } else {
        for (int l = 0; l < lineCount; l++) {
            process_flex_line(container, &lines[l], lines, mainSize, crossPos, lineCount, isRow, isReverse);
            crossPos += lines[l].height + crossGap + lineSpacing;
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